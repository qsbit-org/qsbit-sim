#include "qsbit/tcu.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace qsbit {
TcuCycleModel::TcuCycleModel(const Profile &profile, Trace &trace, std::size_t sync_capacity)
    : profile_(profile), trace_(trace), events_(profile.ports), execution_flags_(profile),
      start_(profile.start), sync_capacity_(sync_capacity) {
  require(sync_capacity > 0, ErrorCode::InvalidProfile, "zero synchronization queue capacity");
}
TcuOutput TcuCycleModel::step(Tick now, Epoch epoch, const TimingEvents *candidate,
                              const std::vector<Completion> &results, const Preflight &preflight,
                              bool paused, const SyncPreflight &sync_preflight) {
  require(profile_.tcu.edge(now), ErrorCode::Protocol, "TCU invoked off-edge");
  const bool running = now >= start_;
  const Tick cycle = running ? (now - start_ - paused_ticks_) / profile_.tcu.period : 0;
  TcuOutput output;
  std::vector<OperationEvent> cancelled;
  const bool fire = running && !paused && !timing_.empty() && timing_.front().due <= cycle;
  if (fire) {
    const auto &point = timing_.front();
    require(point.due == cycle, ErrorCode::LateAdmission, "queued point missed its firing edge");
    std::map<Id, OperationEvent> gathered;
    for (const auto &queue : events_) {
      require(queue.empty() || queue.front().label >= point.point.label,
              ErrorCode::ManifestMismatch, "orphan event precedes timing head");
      for (const auto &event : queue) {
        if (event.label != point.point.label)
          break;
        require(event.epoch == epoch && gathered.emplace(event.id, event).second,
                ErrorCode::ManifestMismatch, "duplicate or stale port event");
      }
    }
    require(gathered.size() == point.point.manifest.size(), ErrorCode::ManifestMismatch,
            "timing manifest and port queues differ");
    TriggeredEvents batch{epoch, point.point.label, now, {}};
    for (auto id : point.point.manifest) {
      const auto it = gathered.find(id);
      require(it != gathered.end(), ErrorCode::ManifestMismatch, "manifested event is missing");
      const auto &event = it->second;
      if (!execution_flags_.evaluate(event.action.targets.front(), event.action.execution_flag))
        cancelled.push_back(event);
      else
        batch.events.push_back(event);
    }
    preflight(batch);
    if (!point.point.synchronizations.empty()) {
      require(bool(sync_preflight), ErrorCode::UnsupportedSynchronization,
              "TCU has no synchronization unit");
      sync_preflight(point.point.synchronizations);
      output.synchronizations = point.point.synchronizations;
    }
    output.launch = std::move(batch);
  }
  Tick new_due = last_due_;
  if (candidate != nullptr) {
    require(!closed_, ErrorCode::Protocol, "request follows stream closure");
    validate_timing_events(*candidate, profile_);
    require(candidate->point.epoch == epoch &&
                candidate->point.label == checked_add(last_label_, 1),
            ErrorCode::Protocol, "request identity is stale, repeated or out of order");
    require(last_label_ == 0 || candidate->point.interval > 0, ErrorCode::Protocol,
            "duplicate logical time point");
    new_due = checked_add(last_due_, candidate->point.interval);
    const auto due_tick =
        checked_add(checked_add(start_, paused_ticks_), checked_mul(new_due, profile_.tcu.period));
    require(now < due_tick, ErrorCode::LateAdmission,
            "request arrived on or after its original deadline");
    bool space = timing_.size() < profile_.timing_capacity;
    require(candidate->point.synchronizations.size() <= sync_capacity_, ErrorCode::Capacity,
            "time point exceeds synchronization queue capacity");
    space = space && candidate->point.synchronizations.size() <= sync_capacity_ - sync_size_;
    std::vector<std::size_t> needed(profile_.ports, 0);
    for (const auto &event : candidate->events)
      ++needed[event.action.port];
    for (std::size_t p = 0; p < events_.size(); ++p)
      space = space && events_[p].size() + needed[p] <= profile_.event_capacity;
    output.admitted = space;
  }
  // Validate all incoming results before committing any launch or admission.
  auto next_flags = execution_flags_;
  for (const auto &result : results)
    next_flags.commit(result, epoch);
  if (fire) {
    const auto label = timing_.front().point.label;
    sync_size_ -= timing_.front().point.synchronizations.size();
    for (auto &queue : events_)
      while (!queue.empty() && queue.front().label == label)
        queue.pop_front();
    timing_.pop_front();
    trace_.emit({now, epoch, "TimingPointTriggered", 0, label, cycle});
    for (const auto &event : cancelled) {
      TraceEvent record{now, epoch, "ConditionCancelled", event.id, label, cycle};
      record.port = event.action.port;
      record.operation = event.action.operation;
      record.targets = event.action.targets;
      trace_.emit(std::move(record));
    }
  }
  if (output.admitted) {
    sync_size_ += candidate->point.synchronizations.size();
    timing_.push_back({candidate->point, new_due});
    for (const auto &event : candidate->events)
      events_[event.action.port].push_back(event);
    last_label_ = candidate->point.label;
    last_due_ = new_due;
    TraceEvent record{now, epoch, "TimingPointEnqueued", 0, last_label_, cycle};
    record.value = timing_.size();
    trace_.emit(std::move(record));
  }
  // Fast results received on this edge cannot affect a condition evaluated above.
  for (const auto &result : results) {
    execution_flags_.commit(result, epoch);
    if (result.reference.epoch == epoch) {
      output.fast_delivered.push_back(result.reference);
      TraceEvent record{now, epoch, "ExecutionFlagsUpdated", result.reference.measurement,
                        0,   cycle};
      record.targets = {result.reference.target};
      record.value = result.value;
      trace_.emit(std::move(record));
    }
  }
  if (running && paused)
    paused_ticks_ = checked_add(paused_ticks_, profile_.tcu.period);
  return output;
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
  last_due_ = 0;
  last_label_ = 0;
  closed_ = false;
  paused_ticks_ = 0;
  sync_size_ = 0;
  const auto proposed = checked_add(epoch_origin, profile_.start);
  start_ = profile_.tcu.edge(proposed) ? proposed : profile_.tcu.after(proposed);
}
} // namespace qsbit
