#include "qsbit/tcu.hpp"
#include "qsbit/control_protocol.hpp"
#include "qsbit/error.hpp"
#include "qsbit/measurement.hpp"
#include "qsbit/time.hpp"
#include "qsbit/timing_config.hpp"
#include "qsbit/trace.hpp"
#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace qsbit {
TcuCycleModel::TcuCycleModel(TcuConfig profile, Trace &trace, std::size_t sync_capacity)
    : profile_(std::move(profile)), trace_(trace), events_(profile_.timing.ports),
      execution_flags_(profile_.timing.qubits), start_(profile_.start),
      sync_capacity_(sync_capacity) {
  require(sync_capacity > 0, ErrorCode::InvalidProfile, "zero synchronization queue capacity");
}
TcuOutput TcuCycleModel::step(Tick now, Epoch epoch, const TimingEvents *candidate,
                              const std::vector<Completion> &results, const Preflight &preflight,
                              bool synchronization_paused, const SyncPreflight &sync_preflight) {
  require(profile_.clock.edge(now), ErrorCode::Protocol, "TCU invoked off-edge");
  const auto state = current_timing_state(now, synchronization_paused);
  auto trigger = prepare_trigger(now, epoch, state, preflight, sync_preflight);
  const auto due =
      candidate ? prepare_admission(now, epoch, *candidate, state.pause) : std::nullopt;
  auto next_flags = execution_flags_;
  for (const auto &result : results)
    next_flags.commit(result, epoch);
  const Tick next_paused_ticks =
      state.pause.paused() ? checked_add(paused_ticks_, profile_.clock.period) : paused_ticks_;

  TcuOutput output;
  if (trigger) {
    commit_trigger(now, epoch, state.cycle, *trigger);
    output.launch = std::move(trigger->batch);
    output.synchronizations = std::move(trigger->synchronizations);
  }
  if (due) {
    commit_admission(now, epoch, state.cycle, *candidate, *due);
    output.admitted = true;
  }
  commit_fast_results(now, epoch, state.cycle, results, std::move(next_flags), output);
  update_pause_state(now, epoch, state, next_paused_ticks);
  return output;
}
UnderflowPolicy TcuCycleModel::policy_after(const TimingPoint &point, UnderflowPolicy previous) {
  return point.underflow_policy == UnderflowPolicy::Inherit ? previous : point.underflow_policy;
}
TcuCycleModel::TimingState TcuCycleModel::current_timing_state(Tick now,
                                                               bool synchronization_paused) const {
  const bool running = now >= start_;
  const TcuCycle cycle{running ? (now - start_ - paused_ticks_) / profile_.clock.period : 0};
  const PauseState pause{running && triggered_policy_ == UnderflowPolicy::PauseWhenEmpty &&
                             timing_.empty() && !closed_,
                         running && synchronization_paused};
  return {running, cycle, pause};
}
bool TcuCycleModel::meets_deadline(Tick now, Tick due_tick, const PauseState &pause) const {
  if (now < due_tick)
    return true;
  return admitted_policy_ == UnderflowPolicy::PauseWhenEmpty && pause.instruction_supply &&
         now == due_tick;
}
std::optional<TcuCycleModel::Trigger>
TcuCycleModel::prepare_trigger(Tick now, Epoch epoch, const TimingState &state,
                               const Preflight &preflight,
                               const SyncPreflight &sync_preflight) const {
  if (!state.running || state.pause.paused() || timing_.empty() ||
      timing_.front().due > state.cycle)
    return std::nullopt;
  const auto &point = timing_.front();
  require(point.due == state.cycle, ErrorCode::LateAdmission,
          "queued point missed its firing edge");
  std::map<Id, OperationEvent> gathered;
  for (const auto &queue : events_) {
    require(queue.empty() || queue.front().label >= point.point.label, ErrorCode::ManifestMismatch,
            "orphan event precedes timing head");
    for (const auto &event : queue) {
      if (event.label != point.point.label)
        break;
      require(event.epoch == epoch && gathered.emplace(event.id, event).second,
              ErrorCode::ManifestMismatch, "duplicate or stale port event");
    }
  }
  require(gathered.size() == point.point.manifest.size(), ErrorCode::ManifestMismatch,
          "timing manifest and port queues differ");
  Trigger trigger{{epoch, point.point.label, now, {}}, {}, point.point.synchronizations};
  for (auto id : point.point.manifest) {
    const auto it = gathered.find(id);
    require(it != gathered.end(), ErrorCode::ManifestMismatch, "manifested event is missing");
    const auto &event = it->second;
    if (event.action.execution_flag() != ExecutionFlag::Always &&
        !execution_flags_.evaluate(event.action.targets().front(), event.action.execution_flag()))
      trigger.cancelled.push_back(event);
    else
      trigger.batch.events.push_back(event);
  }
  preflight(trigger.batch);
  if (!trigger.synchronizations.empty()) {
    require(bool(sync_preflight), ErrorCode::UnsupportedSynchronization,
            "TCU has no synchronization unit");
    sync_preflight(trigger.synchronizations);
  }
  return trigger;
}
std::optional<TcuCycle> TcuCycleModel::prepare_admission(Tick now, Epoch epoch,
                                                         const TimingEvents &candidate,
                                                         const PauseState &pause) const {
  require(!closed_, ErrorCode::Protocol, "request follows stream closure");
  validate_timing_events(candidate, profile_.timing);
  require(candidate.point.epoch == epoch && candidate.point.label == checked_add(last_label_, 1),
          ErrorCode::Protocol, "request identity is stale, repeated or out of order");
  require(last_label_ == 0 || candidate.point.interval > 0 ||
              admitted_policy_ == UnderflowPolicy::PauseWhenEmpty ||
              candidate.point.underflow_policy == UnderflowPolicy::PauseWhenEmpty,
          ErrorCode::Protocol, "duplicate logical time point");
  const auto interval =
      last_label_ == 0 ? candidate.point.interval : std::max<Tick>(1, candidate.point.interval);
  const TcuCycle due{checked_add(last_due_.value, interval)};
  const auto due_tick = checked_add(checked_add(start_, paused_ticks_),
                                    checked_mul(due.value, profile_.clock.period));
  require(meets_deadline(now, due_tick, pause), ErrorCode::LateAdmission,
          "request arrived on or after its original deadline");
  bool space = timing_.size() < profile_.timing_capacity;
  require(candidate.point.synchronizations.size() <= sync_capacity_, ErrorCode::Capacity,
          "time point exceeds synchronization queue capacity");
  space = space && candidate.point.synchronizations.size() <= sync_capacity_ - sync_size_;
  std::vector<std::size_t> needed(profile_.timing.ports, 0);
  for (const auto &event : candidate.events)
    ++needed[event.action.port];
  for (std::size_t p = 0; p < events_.size(); ++p)
    space = space && events_[p].size() + needed[p] <= profile_.timing.event_capacity;
  return space ? std::optional<TcuCycle>{due} : std::nullopt;
}
void TcuCycleModel::commit_trigger(Tick now, Epoch epoch, TcuCycle cycle, const Trigger &trigger) {
  const auto &point = timing_.front().point;
  triggered_policy_ = policy_after(point, triggered_policy_);
  if (point.underflow_policy == UnderflowPolicy::PauseWhenEmpty)
    trace_.emit({now, epoch, "WaitZeroExecuted", 0, point.label, cycle.value});
  const auto label = point.label;
  sync_size_ -= point.synchronizations.size();
  for (auto &queue : events_)
    while (!queue.empty() && queue.front().label == label)
      queue.pop_front();
  timing_.pop_front();
  trace_.emit({now, epoch, "TimingPointTriggered", 0, label, cycle.value});
  for (const auto &event : trigger.cancelled) {
    TraceEvent record{now, epoch, "ConditionCancelled", event.id, label, cycle.value};
    record.port = event.action.port;
    record.operation = event.action.operation();
    record.targets = event.action.targets();
    trace_.emit(std::move(record));
  }
}
void TcuCycleModel::commit_admission(Tick now, Epoch epoch, TcuCycle cycle,
                                     const TimingEvents &candidate, TcuCycle due) {
  admitted_policy_ = policy_after(candidate.point, admitted_policy_);
  sync_size_ += candidate.point.synchronizations.size();
  timing_.push_back({candidate.point, due});
  for (const auto &event : candidate.events)
    events_[event.action.port].push_back(event);
  last_label_ = candidate.point.label;
  last_due_ = due;
  TraceEvent record{now, epoch, "TimingPointEnqueued", 0, last_label_, cycle.value};
  record.value = timing_.size();
  trace_.emit(std::move(record));
}
void TcuCycleModel::commit_fast_results(Tick now, Epoch epoch, TcuCycle cycle,
                                        const std::vector<Completion> &results,
                                        ExecutionFlags next_flags, TcuOutput &output) {
  execution_flags_ = std::move(next_flags);
  for (const auto &result : results) {
    if (result.reference.epoch == epoch) {
      output.fast_delivered.push_back(result.reference);
      TraceEvent record{now, epoch,      "ExecutionFlagsUpdated", result.reference.measurement,
                        0,   cycle.value};
      record.targets = {result.reference.target};
      record.value = result.value;
      trace_.emit(std::move(record));
    }
  }
}
void TcuCycleModel::update_pause_state(Tick now, Epoch epoch, const TimingState &state,
                                       Tick paused_ticks) {
  if (state.pause.paused() != pause_.paused()) {
    TraceEvent event{now, epoch, state.pause.paused() ? "TimerPaused" : "TimerResumed",
                     0,   0,     state.cycle.value};
    event.detail = state.pause.synchronization && state.pause.instruction_supply
                       ? "synchronization and instruction supply"
                   : state.pause.synchronization    ? "synchronization"
                   : state.pause.instruction_supply ? "instruction supply"
                                                    : "";
    trace_.emit(std::move(event));
  }
  pause_ = state.pause;
  paused_ticks_ = paused_ticks;
}
void TcuCycleModel::close(const EndOfStream &end) {
  require(!closed_ && end.last_label == last_label_, ErrorCode::Protocol,
          "invalid or repeated stream closure");
  closed_ = true;
}
bool TcuCycleModel::drained() const {
  return closed_ && timing_.empty() &&
         std::all_of(events_.begin(), events_.end(),
                     [](const auto &queue) { return queue.empty(); });
}
void TcuCycleModel::reset(Tick epoch_origin) {
  timing_.clear();
  for (auto &queue : events_)
    queue.clear();
  execution_flags_.reset();
  last_due_ = {};
  last_label_ = 0;
  closed_ = false;
  admitted_policy_ = UnderflowPolicy::Strict;
  triggered_policy_ = UnderflowPolicy::Strict;
  pause_ = {};
  paused_ticks_ = 0;
  sync_size_ = 0;
  const auto proposed = checked_add(epoch_origin, profile_.start);
  start_ = profile_.clock.edge(proposed) ? proposed : profile_.clock.after(proposed);
}
} // namespace qsbit
