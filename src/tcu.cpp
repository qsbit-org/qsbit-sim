#include "qsbit/tcu.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace qsbit {
TcuCycleModel::TcuCycleModel(const Profile &profile, Trace &trace)
    : profile_(profile), trace_(trace), events_(profile.ports), history_(profile),
      start_(profile.start) {}
TcuOutput TcuCycleModel::step(Tick now, Epoch epoch, const Group *candidate,
                              const std::vector<Completion> &results, const Preflight &preflight) {
  require(profile_.tcu.edge(now), ErrorCode::Protocol, "TCU invoked off-edge");
  const bool running = now >= start_;
  const Tick cycle = running ? (now - start_) / profile_.tcu.period : 0;
  TcuOutput output;
  std::vector<ReservedEvent> cancelled;
  const bool fire = running && !timing_.empty() && timing_.front().due <= cycle;
  if (fire) {
    const auto &point = timing_.front();
    require(point.due == cycle, ErrorCode::LateAdmission, "queued point missed its firing edge");
    std::map<Id, ReservedEvent> gathered;
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
    LaunchBatch batch{epoch, point.point.label, now, {}};
    for (auto id : point.point.manifest) {
      const auto it = gathered.find(id);
      require(it != gathered.end(), ErrorCode::ManifestMismatch, "manifested event is missing");
      const auto &event = it->second;
      if (event.condition && !history_.evaluate(*event.condition))
        cancelled.push_back(event);
      else
        batch.events.push_back(event);
    }
    preflight(batch);
    output.launch = std::move(batch);
  }
  Tick new_due = last_due_;
  if (candidate != nullptr) {
    require(!closed_, ErrorCode::Protocol, "group follows stream closure");
    validate_group(*candidate, profile_);
    require(candidate->point.epoch == epoch &&
                candidate->point.label == checked_add(last_label_, 1),
            ErrorCode::Protocol, "group identity is stale, repeated or out of order");
    require(last_label_ == 0 || candidate->point.interval > 0, ErrorCode::Protocol,
            "duplicate logical time point");
    new_due = checked_add(last_due_, candidate->point.interval);
    const auto due_tick = checked_add(start_, checked_mul(new_due, profile_.tcu.period));
    require(now < due_tick, ErrorCode::LateAdmission,
            "group arrived on or after its original deadline");
    bool space = timing_.size() < profile_.timing_capacity;
    std::vector<std::size_t> needed(profile_.ports, 0);
    for (const auto &event : candidate->events)
      ++needed[event.action.port];
    for (std::size_t p = 0; p < events_.size(); ++p)
      space = space && events_[p].size() + needed[p] <= profile_.event_capacity;
    output.admitted = space;
  }
  // Validate all incoming history before committing any launch or admission.
  auto next_history = history_;
  for (const auto &result : results)
    next_history.commit(result, epoch);
  if (fire) {
    const auto label = timing_.front().point.label;
    for (auto &queue : events_)
      while (!queue.empty() && queue.front().label == label)
        queue.pop_front();
    timing_.pop_front();
    trace_.emit({now, epoch, "LabelFired", 0, label, cycle});
    for (const auto &event : cancelled) {
      TraceEvent record{now, epoch, "ConditionCancelled", event.id, label, cycle};
      record.port = event.action.port;
      record.operation = event.action.operation;
      record.targets = event.action.targets;
      trace_.emit(std::move(record));
    }
  }
  if (output.admitted) {
    timing_.push_back({candidate->point, new_due});
    for (const auto &event : candidate->events)
      events_[event.action.port].push_back(event);
    last_label_ = candidate->point.label;
    last_due_ = new_due;
    TraceEvent record{now, epoch, "GroupAdmitted", 0, last_label_, cycle};
    record.value = timing_.size();
    trace_.emit(std::move(record));
  }
  // Fast results received on this edge cannot affect a condition evaluated above.
  for (const auto &result : results) {
    history_.commit(result, epoch);
    if (result.token.epoch == epoch) {
      output.fast_delivered.push_back(result.token);
      TraceEvent record{now, epoch, "FastResultVisible", result.token.measurement, 0, cycle};
      record.targets = {result.token.target};
      record.value = result.value;
      trace_.emit(std::move(record));
    }
  }
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
  history_.reset();
  last_due_ = 0;
  last_label_ = 0;
  closed_ = false;
  const auto proposed = checked_add(epoch_origin, profile_.start);
  start_ = profile_.tcu.edge(proposed) ? proposed : profile_.tcu.after(proposed);
}
} // namespace qsbit
