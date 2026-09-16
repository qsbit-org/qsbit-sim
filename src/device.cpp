#include "qsbit/device.hpp"
#include <algorithm>
#include <set>

namespace qsbit {
namespace {
bool common_target(const ActionSpec &a, const ActionSpec &b) {
  return std::any_of(a.targets.begin(), a.targets.end(), [&](auto q) {
    return std::find(b.targets.begin(), b.targets.end(), q) != b.targets.end();
  });
}
bool is_arm(const ActionSpec &a) { return a.kind == ActionKind::DiscriminatorArm; }
} // namespace
void ResourceCalendar::pair(const PhysicalAction &a, const PhysicalAction &b) {
  const auto &x = a.event.action;
  const auto &y = b.event.action;
  const bool overlap = a.start < b.end && b.start < a.end;
  if (overlap) {
    require(x.port != y.port, ErrorCode::ResourceConflict, "output port intervals overlap");
    for (const auto &rx : x.resources)
      for (const auto &ry : y.resources)
        require(rx.id != ry.id || (!rx.exclusive && !ry.exclusive), ErrorCode::ResourceConflict,
                "exclusive resource intervals overlap");
    if (!is_arm(x) && !is_arm(y) && common_target(x, y))
      require(x.kind == ActionKind::Pulse && y.kind == ActionKind::Pulse,
              ErrorCode::ResourceConflict, "incompatible quantum actions overlap on one target");
  }
  if (common_target(x, y)) {
    const bool measurement_gate =
        (x.kind == ActionKind::Acquire && y.kind == ActionKind::IdealGate && a.end == b.start) ||
        (y.kind == ActionKind::Acquire && x.kind == ActionKind::IdealGate && b.end == a.start);
    require(!measurement_gate, ErrorCode::ResourceConflict,
            "measurement sample and ideal gate share a target and tick");
  }
}
void ResourceCalendar::check(std::span<const PhysicalAction> actions) const {
  std::set<Id> ids;
  for (std::size_t i = 0; i < actions.size(); ++i) {
    const auto &action = actions[i];
    require(ids.insert(action.event.id).second, ErrorCode::Protocol, "duplicate action in launch");
    for (const auto &old : reservations_) {
      require(old.event.id != action.event.id, ErrorCode::Protocol, "action was already reserved");
      pair(action, old);
    }
    for (std::size_t j = 0; j < i; ++j)
      pair(action, actions[j]);
  }
}
void ResourceCalendar::reserve(std::span<const PhysicalAction> actions) {
  check(actions);
  reservations_.insert(reservations_.end(), actions.begin(), actions.end());
}
void ResourceCalendar::discard_before(Tick now) {
  std::erase_if(reservations_, [&](const PhysicalAction &action) { return action.end < now; });
}
DeviceRuntime::DeviceRuntime(const Profile &profile, IQuantumBackend &backend, Trace &trace)
    : profile_(profile), backend_(backend), trace_(trace) {
  backend_.reset(profile.qubits, profile.seed);
}
std::vector<PhysicalAction> DeviceRuntime::resolve(const LaunchBatch &batch) const {
  std::vector<PhysicalAction> actions;
  for (const auto &event : batch.events) {
    require(event.epoch == batch.epoch && event.label == batch.label, ErrorCode::Protocol,
            "launch event identity mismatch");
    backend_.validate(event.action);
    const Tick start = checked_add(batch.fire_tick, event.action.delay);
    actions.push_back({event, start, checked_add(start, event.action.duration)});
  }
  return actions;
}
void DeviceRuntime::preflight(const LaunchBatch &batch) const {
  const auto actions = resolve(batch);
  calendar_.check(actions);
  std::map<Id, unsigned> acquisitions, arms;
  std::map<Id, Token> identities;
  for (const auto &action : actions) {
    const auto &e = action.event;
    if (e.action.kind == ActionKind::Acquire || is_arm(e.action)) {
      require(e.token && e.token->epoch == batch.epoch, ErrorCode::InvalidToken,
              "readout token missing or stale");
      require(e.action.targets.size() == 1 && e.action.targets.front() == e.token->target,
              ErrorCode::InvalidToken, "readout target and token differ");
      const auto [identity, inserted] = identities.emplace(e.token->measurement, *e.token);
      require(inserted || identity->second == *e.token, ErrorCode::InvalidToken,
              "readout members disagree on token identity");
      if (is_arm(e.action))
        ++arms[e.token->measurement];
      else
        ++acquisitions[e.token->measurement];
    }
  }
  for (const auto &action : actions)
    if (action.event.action.kind == ActionKind::Acquire) {
      const auto id = action.event.token->measurement;
      require(acquisitions[id] == 1 && !readouts_.contains(id) &&
                  arms[id] == (action.event.action.separate_arm ? 1U : 0U),
              ErrorCode::Protocol, "invalid acquisition and arm pairing");
      Tick arm = action.start;
      if (action.event.action.separate_arm)
        for (const auto &other : actions)
          if (is_arm(other.event.action) && other.event.token == action.event.token)
            arm = other.start;
      (void)checked_add(std::max(action.end, arm), action.event.action.discriminator_delay);
    }
  for (const auto &[id, count] : arms)
    if (count > 0)
      require(acquisitions[id] == 1, ErrorCode::Protocol, "orphan discriminator arm");
  require(!processed_tick_ || batch.fire_tick > *processed_tick_, ErrorCode::Protocol,
          "launch arrived after its physical barrier");
}
void DeviceRuntime::accept(const LaunchBatch &batch) {
  preflight(batch);
  const auto actions = resolve(batch);
  calendar_.reserve(actions);
  for (const auto &action : actions) {
    const auto &e = action.event;
    boundaries_[action.start].starts.push_back(action);
    boundaries_[action.end].ends.push_back(e.id);
    if (e.action.kind == ActionKind::Acquire) {
      Tick arm = action.start;
      if (e.action.separate_arm)
        for (const auto &other : actions)
          if (is_arm(other.event.action) && other.event.token == e.token)
            arm = other.start;
      const Tick ready = checked_add(std::max(action.end, arm), e.action.discriminator_delay);
      readouts_.emplace(e.token->measurement, Readout{*e.token, action.end, arm, ready, {}});
      boundaries_[ready].ready.push_back(e.token->measurement);
    }
    TraceEvent record{batch.fire_tick, batch.epoch, "CodewordTriggered", e.id, batch.label};
    record.port = e.action.port;
    record.codeword = e.codeword;
    record.targets = e.action.targets;
    record.operation = e.action.operation;
    trace_.emit(std::move(record));
  }
}
void DeviceRuntime::process(Tick now, Epoch epoch, ControlLinks &links) {
  require(now >= last_tick_ && (!processed_tick_ || now > *processed_tick_), ErrorCode::Protocol,
          "physical boundary replay or decreasing time");
  const auto it = boundaries_.find(now);
  if (it == boundaries_.end())
    return;
  const auto &boundary = it->second;
  std::vector<Token> samples;
  std::vector<ActionSpec> gates, drives;
  for (const auto &[id, action] : active_) {
    (void)id;
    if (action.event.action.kind == ActionKind::Pulse)
      drives.push_back(action.event.action);
  }
  for (auto id : boundary.ends) {
    const auto action = active_.find(id);
    require(action != active_.end(), ErrorCode::Protocol, "physical end has no active action");
    if (action->second.event.action.kind == ActionKind::Acquire)
      samples.push_back(*action->second.event.token);
  }
  std::set<std::uint32_t> sampled_targets;
  for (const auto &token : samples)
    require(sampled_targets.insert(token.target).second, ErrorCode::ResourceConflict,
            "duplicate same-tick measurement target");
  for (const auto &action : boundary.starts) {
    require(action.event.epoch == epoch, ErrorCode::Protocol,
            "old epoch reached physical boundary");
    backend_.validate(action.event.action);
    if (action.event.action.kind == ActionKind::IdealGate) {
      for (auto target : action.event.action.targets)
        require(!sampled_targets.contains(target), ErrorCode::ResourceConflict,
                "sample and gate collide at physical boundary");
      gates.push_back(action.event.action);
    }
  }
  // All capability, identity and ordering checks precede the first backend mutation.
  if (now > last_tick_)
    backend_.evolve(last_tick_, now, drives);
  const auto outcomes = backend_.measure(samples);
  require(outcomes.size() == samples.size(), ErrorCode::BackendFailure,
          "backend returned wrong measurement count");
  for (std::size_t i = 0; i < samples.size(); ++i) {
    readouts_.at(samples[i].measurement).sample = outcomes[i];
    TraceEvent record{now, epoch, "MeasurementSampled", samples[i].measurement};
    record.targets = {samples[i].target};
    record.value = outcomes[i];
    trace_.emit(std::move(record));
  }
  for (auto id : boundary.ends) {
    const auto &action = active_.at(id);
    TraceEvent record{now, epoch, "OperationEnd", id, action.event.label};
    record.port = action.event.action.port;
    record.targets = action.event.action.targets;
    record.operation = action.event.action.operation;
    trace_.emit(std::move(record));
    active_.erase(id);
  }
  backend_.apply(gates);
  for (const auto &action : boundary.starts) {
    require(active_.emplace(action.event.id, action).second, ErrorCode::Protocol,
            "physical action started twice");
    TraceEvent record{now, epoch, "OperationStart", action.event.id, action.event.label};
    record.port = action.event.action.port;
    record.codeword = action.event.codeword;
    record.targets = action.event.action.targets;
    record.operation = action.event.action.operation;
    record.value = action.end - action.start;
    trace_.emit(std::move(record));
  }
  for (auto id : boundary.ready) {
    const auto &readout = readouts_.at(id);
    require(readout.sample.has_value(), ErrorCode::Protocol, "result is ready before sampling");
    Completion result{readout.token, *readout.sample};
    links.cpu_results.publish(now, epoch, result);
    if (profile_.fast_feedback)
      links.fast_results.publish(now, epoch, result);
    TraceEvent record{now, epoch, "ResultReady", id};
    record.targets = {readout.token.target};
    record.value = result.value;
    trace_.emit(std::move(record));
    readouts_.erase(id);
  }
  last_tick_ = now;
  processed_tick_ = now;
  boundaries_.erase(it);
  calendar_.discard_before(now);
}
std::optional<Tick> DeviceRuntime::next_boundary() const {
  return boundaries_.empty() ? std::nullopt : std::optional<Tick>{boundaries_.begin()->first};
}
void DeviceRuntime::reset(Tick now, Epoch epoch) {
  for (const auto &action : calendar_.reservations())
    if (action.end >= now)
      trace_.emit({now, epoch, "ResetAborted", action.event.id, action.event.label});
  boundaries_.clear();
  active_.clear();
  readouts_.clear();
  calendar_.reset();
  backend_.reset(profile_.qubits, profile_.seed);
  last_tick_ = now;
  processed_tick_.reset();
}
} // namespace qsbit
