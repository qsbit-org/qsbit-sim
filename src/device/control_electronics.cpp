#include "action_kind.hpp"
#include "qsbit/backend.hpp"
#include "qsbit/control_protocol.hpp"
#include "qsbit/device.hpp"
#include "qsbit/device/gates.hpp"
#include "qsbit/error.hpp"
#include "qsbit/event.hpp"
#include "qsbit/time.hpp"
#include "qsbit/trace.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace qsbit {
ControlElectronics::ControlElectronics(DeviceProfile profile, IQuantumBackend &backend,
                                       Trace &trace, BackendExecutionConfig execution)
    : profile_(std::move(profile)), backend_(backend, execution), trace_(trace) {
  backend_.reset(profile_.qubits, profile_.seed, 1);
}
void ControlElectronics::validate(const EventSpec &action) const {
  backend_.validate(action.kind() == ActionKind::GateOutput
                        ? gate_action(profile_, action.get<GateOutputSpec>().gate)
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
      resolved.spec = gate_action(profile_, gate.name).spec;
    }
    actions.push_back(
        {event, start, checked_add(start, event.action.duration), std::move(resolved)});
  }
  return actions;
}
ControlElectronics::PreparedEvents ControlElectronics::prepare(const TriggeredEvents &batch) const {
  auto actions = resolve(batch);
  reservations_.check(actions);
  auto readouts = readouts_.prepare(actions, batch.epoch);
  require(!processed_tick_ || batch.fire_tick > *processed_tick_, ErrorCode::Protocol,
          "launch arrived after its physical barrier");
  return {std::move(actions), std::move(readouts)};
}
void ControlElectronics::preflight(const TriggeredEvents &batch) const { (void)prepare(batch); }
void ControlElectronics::accept(const TriggeredEvents &batch) {
  const auto prepared = prepare(batch);
  const auto &actions = prepared.actions;
  reservations_.reserve(actions);
  readouts_.insert(prepared.readouts);
  for (const auto &readout : prepared.readouts)
    boundaries_[readout.ready].ready.push_back(readout.reference.measurement);
  for (const auto &action : actions) {
    const auto &e = action.event;
    boundaries_[action.start].starts.push_back(action);
    boundaries_[action.end].ends.push_back(e.id);
    TraceEvent record{batch.fire_tick, batch.epoch, "CodewordTriggered", e.id, batch.label};
    record.core = e.core;
    record.port = e.action.port;
    record.codeword = e.codeword;
    record.targets = action.resolved.targets();
    record.operation = action.resolved.operation();
    trace_.emit(std::move(record));
  }
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
  const auto paired_gates = resolve_gate_outputs(profile_, now, gate_outputs);
  gates.insert(gates.end(), paired_gates.begin(), paired_gates.end());
  // All capability, identity and ordering checks precede the first backend mutation.
  if (now > last_tick_)
    backend_.evolve(last_tick_, now, drives, acquisitions);
  const auto outcomes = backend_.measure(now, samples);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    readouts_.sample(samples[i].measurement, outcomes[i]);
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
    const auto result = readouts_.result(id);
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
  readouts_.reset();
  reservations_.reset();
  backend_.reset(profile_.qubits, profile_.seed, epoch, now);
  last_tick_ = now;
  processed_tick_.reset();
}
} // namespace qsbit
