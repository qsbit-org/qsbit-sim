#include "qsbit/device/gates.hpp"
#include "qsbit/control_protocol.hpp"
#include "qsbit/error.hpp"
#include "qsbit/event.hpp"
#include "qsbit/profile.hpp"
#include "qsbit/time.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace qsbit {
DeviceProfile device_profile(const Profile &profile) {
  DeviceProfile result{profile.qubits, profile.seed, {}};
  for (const auto &gate : profile.two_qubit_gates)
    require(result.gates.emplace(gate.name, gate).second, ErrorCode::InvalidProfile,
            "duplicate two-qubit gate: " + gate.name);
  return result;
}
const TwoQubitGate &DeviceProfile::gate(const std::string &name) const {
  const auto found = gates.find(name);
  require(found != gates.end(), ErrorCode::InvalidProfile, "unknown two-qubit gate: " + name);
  return found->second;
}
EventSpec gate_action(const DeviceProfile &profile, const std::string &name) {
  const auto &gate = profile.gate(name);
  EventSpec action;
  action.get<GateSpec>().operation = gate.operation;
  action.get<GateSpec>().operands.targets = gate.targets;
  action.get<GateSpec>().operands.resources = gate.resources;
  action.duration = gate.duration;
  return action;
}
std::vector<EventSpec> resolve_gate_outputs(
    const DeviceProfile &profile, Tick now,
    const std::map<std::string, std::vector<const ScheduledEvent *>> &gate_outputs) {
  std::vector<EventSpec> gates;
  for (const auto &[name, outputs] : gate_outputs) {
    const auto &gate = profile.gate(name);
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
    gates.push_back(gate_action(profile, name));
  }
  return gates;
}
} // namespace qsbit
