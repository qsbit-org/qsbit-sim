#include "qsbit/device.hpp"
#include <algorithm>
#include <set>

namespace qsbit {
namespace {
bool common_target(const EventSpec &a, const EventSpec &b) {
  return std::any_of(a.targets().begin(), a.targets().end(), [&](auto q) {
    return std::find(b.targets().begin(), b.targets().end(), q) != b.targets().end();
  });
}
bool is_arm(const EventSpec &a) { return a.kind() == ActionKind::DiscriminatorArm; }
bool is_gate(const EventSpec &a) {
  return a.kind() == ActionKind::IdealGate || a.kind() == ActionKind::GateOutput;
}
} // namespace
void ResourceReservations::pair(const ScheduledEvent &a, const ScheduledEvent &b) {
  const auto &x = a.resolved;
  const auto &y = b.resolved;
  const bool overlap = a.start < b.end && b.start < a.end;
  if (overlap) {
    require(x.port != y.port, ErrorCode::ResourceConflict, "output port intervals overlap");
    if (a.event.action.kind() == ActionKind::GateOutput &&
        b.event.action.kind() == ActionKind::GateOutput &&
        a.event.action.get<GateOutputSpec>().gate == b.event.action.get<GateOutputSpec>().gate) {
      require(a.start == b.start && a.end == b.end, ErrorCode::GateInputMismatch,
              "two-qubit gate output intervals differ");
      return;
    }
    for (const auto &rx : x.resources())
      for (const auto &ry : y.resources())
        require(rx.id != ry.id || (!rx.exclusive && !ry.exclusive), ErrorCode::ResourceConflict,
                "exclusive resource intervals overlap");
    if (!is_arm(x) && !is_arm(y) && common_target(x, y))
      require(x.kind() == ActionKind::Pulse && y.kind() == ActionKind::Pulse,
              ErrorCode::ResourceConflict, "incompatible quantum actions overlap on one target");
  }
  if (common_target(x, y)) {
    const bool measurement_gate =
        (x.kind() == ActionKind::Acquire && is_gate(y) && a.end == b.start) ||
        (y.kind() == ActionKind::Acquire && is_gate(x) && b.end == a.start);
    require(!measurement_gate, ErrorCode::ResourceConflict,
            "measurement sample and ideal gate share a target and tick");
  }
}
void ResourceReservations::check(std::span<const ScheduledEvent> actions) const {
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
void ResourceReservations::reserve(std::span<const ScheduledEvent> actions) {
  check(actions);
  reservations_.insert(reservations_.end(), actions.begin(), actions.end());
}
void ResourceReservations::discard_before(Tick now) {
  std::erase_if(reservations_, [&](const ScheduledEvent &action) { return action.end < now; });
}
ControlElectronics::ControlElectronics(const Profile &profile, IQuantumBackend &backend,
                                       Trace &trace, BackendExecutionConfig execution)
    : profile_(profile), backend_(backend, execution), trace_(trace) {
  backend_.reset(profile.qubits, profile.seed, 1);
}
EventSpec ControlElectronics::gate_action(const std::string &name) const {
  const auto &gate = profile_.gate(name);
  EventSpec action;
  action.get<GateSpec>().operation = gate.operation;
  action.get<GateSpec>().targets = gate.targets;
  action.get<GateSpec>().resources = gate.resources;
  action.duration = gate.duration;
  return action;
}
void ControlElectronics::validate(const EventSpec &action) const {
  backend_.validate(action.kind() == ActionKind::GateOutput
                        ? gate_action(action.get<GateOutputSpec>().gate)
                        : action);
}
std::vector<ScheduledEvent> ControlElectronics::resolve(const TriggeredEvents &batch) const {
  std::vector<ScheduledEvent> actions;
  for (const auto &event : batch.events) {
    require(event.epoch == batch.epoch && event.label == batch.label, ErrorCode::Protocol,
            "launch event identity mismatch");
    validate(event.action);
    const Tick start = checked_add(batch.fire_tick, event.action.delay);
    auto resolved = event.action;
    if (event.action.kind() == ActionKind::GateOutput) {
      const auto &gate = profile_.gate(event.action.get<GateOutputSpec>().gate);
      const GateInput input{event.core.value_or(0), event.source_port, event.codeword};
      require(std::find(gate.inputs.begin(), gate.inputs.end(), input) != gate.inputs.end() &&
                  event.action.duration == gate.duration,
              ErrorCode::GateInputMismatch, "unexpected gate output endpoint or duration");
      resolved.spec = gate_action(gate.name).spec;
    }
    actions.push_back(
        {event, start, checked_add(start, event.action.duration), std::move(resolved)});
  }
  return actions;
}
void ControlElectronics::preflight(const TriggeredEvents &batch) const {
  const auto actions = resolve(batch);
  reservations_.check(actions);
  std::map<Id, unsigned> acquisitions, arms;
  std::map<Id, MeasurementReference> identities;
  for (const auto &action : actions) {
    const auto &e = action.event;
    if (e.action.kind() == ActionKind::Acquire || is_arm(e.action)) {
      require(e.reference && e.reference->epoch == batch.epoch, ErrorCode::InvalidMeasurement,
              "readout reference missing or stale");
      require(e.action.targets().size() == 1 && e.action.targets().front() == e.reference->target,
              ErrorCode::InvalidMeasurement, "readout target and reference differ");
      const auto [identity, inserted] = identities.emplace(e.reference->measurement, *e.reference);
      require(inserted || identity->second == *e.reference, ErrorCode::InvalidMeasurement,
              "readout members disagree on reference identity");
      if (is_arm(e.action))
        ++arms[e.reference->measurement];
      else
        ++acquisitions[e.reference->measurement];
    }
  }
  for (const auto &action : actions)
    if (action.event.action.kind() == ActionKind::Acquire) {
      const auto id = action.event.reference->measurement;
      require(acquisitions[id] == 1 && !readouts_.contains(id) &&
                  arms[id] == (action.event.action.get<AcquireSpec>().separate_arm ? 1U : 0U),
              ErrorCode::Protocol, "invalid acquisition and arm pairing");
      Tick arm = action.start;
      if (action.event.action.get<AcquireSpec>().separate_arm)
        for (const auto &other : actions)
          if (is_arm(other.event.action) && other.event.reference == action.event.reference)
            arm = other.start;
      (void)checked_add(std::max(action.end, arm),
                        action.event.action.get<AcquireSpec>().discriminator_delay);
    }
  for (const auto &[id, count] : arms)
    if (count > 0)
      require(acquisitions[id] == 1, ErrorCode::Protocol, "orphan discriminator arm");
  require(!processed_tick_ || batch.fire_tick > *processed_tick_, ErrorCode::Protocol,
          "launch arrived after its physical barrier");
}
void ControlElectronics::accept(const TriggeredEvents &batch) {
  preflight(batch);
  const auto actions = resolve(batch);
  reservations_.reserve(actions);
  for (const auto &action : actions) {
    const auto &e = action.event;
    boundaries_[action.start].starts.push_back(action);
    boundaries_[action.end].ends.push_back(e.id);
    if (e.action.kind() == ActionKind::Acquire) {
      Tick arm = action.start;
      if (e.action.get<AcquireSpec>().separate_arm)
        for (const auto &other : actions)
          if (is_arm(other.event.action) && other.event.reference == e.reference)
            arm = other.start;
      const Tick ready =
          checked_add(std::max(action.end, arm), e.action.get<AcquireSpec>().discriminator_delay);
      readouts_.emplace(e.reference->measurement,
                        Readout{*e.reference, action.end, arm, ready, {}, e.core});
      boundaries_[ready].ready.push_back(e.reference->measurement);
    }
    TraceEvent record{batch.fire_tick, batch.epoch, "CodewordTriggered", e.id, batch.label};
    record.core = e.core;
    record.port = e.action.port;
    record.codeword = e.codeword;
    record.targets = action.resolved.targets();
    record.operation = action.resolved.operation();
    trace_.emit(std::move(record));
  }
}
void ControlElectronics::process(Tick now, Epoch epoch, ControlLinks &links) {
  process(now, epoch, [&](const Completion &result) {
    links.cpu_results.publish(now, epoch, result);
    if (profile_.fast_feedback)
      links.fast_results.publish(now, epoch, result);
  });
}
void ControlElectronics::process(Tick now, Epoch epoch, const Deliver &deliver) {
  require(now >= last_tick_ && (!processed_tick_ || now > *processed_tick_), ErrorCode::Protocol,
          "physical boundary replay or decreasing time");
  const auto it = boundaries_.find(now);
  if (it == boundaries_.end())
    return;
  const auto &boundary = it->second;
  std::vector<MeasurementReference> samples;
  std::vector<EventSpec> gates;
  std::vector<BackendActivity> drives, acquisitions;
  std::map<std::string, std::vector<const ScheduledEvent *>> gate_outputs;
  for (const auto &[id, action] : active_) {
    if (action.event.action.kind() == ActionKind::Pulse)
      drives.push_back({id, action.start, action.end, action.event.action, {}});
    if (action.event.action.kind() == ActionKind::Acquire)
      acquisitions.push_back(
          {id, action.start, action.end, action.event.action, action.event.reference});
  }
  for (auto id : boundary.ends) {
    const auto action = active_.find(id);
    require(action != active_.end(), ErrorCode::Protocol, "physical end has no active action");
    if (action->second.event.action.kind() == ActionKind::Acquire)
      samples.push_back(*action->second.event.reference);
  }
  std::set<std::uint32_t> sampled_targets;
  for (const auto &reference : samples)
    require(sampled_targets.insert(reference.target).second, ErrorCode::ResourceConflict,
            "duplicate same-tick measurement target");
  for (const auto &action : boundary.starts) {
    require(action.event.epoch == epoch, ErrorCode::Protocol,
            "old epoch reached physical boundary");
    validate(action.event.action);
    if (is_gate(action.event.action)) {
      for (auto target : action.resolved.targets())
        require(!sampled_targets.contains(target), ErrorCode::ResourceConflict,
                "sample and gate collide at physical boundary");
      if (action.event.action.kind() == ActionKind::GateOutput)
        gate_outputs[action.event.action.get<GateOutputSpec>().gate].push_back(&action);
      else
        gates.push_back(action.event.action);
    }
  }
  for (const auto &[name, outputs] : gate_outputs) {
    const auto &gate = profile_.gate(name);
    require(outputs.size() == gate.inputs.size(), ErrorCode::GateInputMismatch,
            "two-qubit gate requires both outputs at the same physical start tick: " + name);
    for (const auto &input : gate.inputs)
      require(std::count_if(outputs.begin(), outputs.end(),
                            [&](const auto *output) {
                              const auto &event = output->event;
                              return input == GateInput{event.core.value_or(0), event.source_port,
                                                        event.codeword} &&
                                     output->end == checked_add(now, gate.duration);
                            }) == 1,
              ErrorCode::GateInputMismatch, "missing or duplicate two-qubit gate input");
    gates.push_back(gate_action(name));
  }
  // All capability, identity and ordering checks precede the first backend mutation.
  if (now > last_tick_)
    backend_.evolve(last_tick_, now, drives, acquisitions);
  const auto outcomes = backend_.measure(now, samples);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    readouts_.at(samples[i].measurement).sample = outcomes[i];
    TraceEvent record{now, epoch, "MeasurementSampled", samples[i].measurement};
    record.core = readouts_.at(samples[i].measurement).core;
    record.targets = {samples[i].target};
    record.value = outcomes[i];
    trace_.emit(std::move(record));
  }
  for (auto id : boundary.ends) {
    const auto &action = active_.at(id);
    TraceEvent record{now, epoch, "OperationEnd", id, action.event.label};
    record.core = action.event.core;
    record.port = action.event.action.port;
    record.targets = action.resolved.targets();
    record.operation = action.resolved.operation();
    trace_.emit(std::move(record));
    active_.erase(id);
  }
  backend_.apply(now, gates);
  for (const auto &[name, outputs] : gate_outputs) {
    (void)outputs;
    const auto &gate = profile_.gate(name);
    TraceEvent record{now, epoch, "GateApplied"};
    record.operation = gate.operation;
    record.targets = gate.targets;
    record.value = gate.duration;
    record.detail = name;
    trace_.emit(std::move(record));
  }
  for (const auto &action : boundary.starts) {
    require(active_.emplace(action.event.id, action).second, ErrorCode::Protocol,
            "physical action started twice");
    TraceEvent record{now, epoch, "OperationStart", action.event.id, action.event.label};
    record.core = action.event.core;
    record.port = action.event.action.port;
    record.codeword = action.event.codeword;
    record.targets = action.resolved.targets();
    record.operation = action.resolved.operation();
    record.value = action.end - action.start;
    trace_.emit(std::move(record));
  }
  for (auto id : boundary.ready) {
    const auto &readout = readouts_.at(id);
    require(readout.sample.has_value(), ErrorCode::Protocol, "result is ready before sampling");
    Completion result{readout.reference, *readout.sample};
    TraceEvent record{now, epoch, "ResultReady", id};
    deliver(result);
    record.core = readout.core;
    record.targets = {readout.reference.target};
    record.value = result.value;
    trace_.emit(std::move(record));
    readouts_.erase(id);
  }
  last_tick_ = now;
  processed_tick_ = now;
  boundaries_.erase(it);
  reservations_.discard_before(now);
}
std::optional<Tick> ControlElectronics::next_boundary() const {
  return boundaries_.empty() ? std::nullopt : std::optional<Tick>{boundaries_.begin()->first};
}
void ControlElectronics::finalize(Tick now) {
  require(drained(), ErrorCode::Protocol, "cannot finalize pending device work");
  require(now >= last_tick_, ErrorCode::Protocol, "final state time precedes device time");
  if (now > last_tick_)
    backend_.evolve(last_tick_, now, {});
  last_tick_ = now;
  backend_.flush();
}
void ControlElectronics::reset(Tick now, Epoch epoch) {
  for (const auto &action : reservations_.reservations())
    if (action.end >= now)
      trace_.emit({now, epoch, "ResetAborted", action.event.id, action.event.label});
  boundaries_.clear();
  active_.clear();
  readouts_.clear();
  reservations_.reset();
  backend_.reset(profile_.qubits, profile_.seed, epoch, now);
  last_tick_ = now;
  processed_tick_.reset();
}
} // namespace qsbit
