#include "qsbit/defaults.hpp"
#include <numbers>

namespace qsbit {
Profile default_profile() {
  Profile p;
  for (std::uint32_t q = 0; q < p.qubits; ++q) {
    for (std::uint32_t code = 1; code <= 5; ++code) {
      EventSpec a;
      a.port = q;
      const QuantumOperands common{{q}, {{q, true}}};
      if (code <= 3) {
        GateSpec gate;
        gate.operands = common;
        gate.operation = code == 1 ? "x" : code == 2 ? "h" : "z";
        a.spec = gate;
      } else if (code == 4) {
        AcquireSpec acquisition;
        acquisition.operands = common;
        acquisition.operation = "measure";
        a.spec = acquisition;
        a.duration = 40;
      } else {
        PulseSpec pulse;
        pulse.operands = common;
        pulse.operation = "drive_x";
        pulse.amplitude = std::numbers::pi / 20.0;
        a.spec = pulse;
      }
      p.mappings.push_back({q, code, {a}});
    }
  }
  p.two_qubit_gates.push_back(
      {"cx01", "cx", {0, 1}, {{0, 0, 6}, {0, 1, 10}}, {{0, true}, {1, true}}, 20});
  for (auto [port, code] : {std::pair{0U, 6U}, std::pair{1U, 10U}}) {
    EventSpec output;
    output.spec = GateOutputSpec{"cx01"};
    output.port = port;
    p.mappings.push_back({port, code, {output}});
  }
  for (std::uint32_t q = 0; q < p.qubits; ++q) {
    for (auto flag : {ExecutionFlag::LastOne, ExecutionFlag::LastZero, ExecutionFlag::Equal}) {
      EventSpec action;
      action.port = q;
      action.get<GateSpec>().operands.targets = {q};
      action.get<GateSpec>().operands.resources = {{q, true}};
      action.get<GateSpec>().execution_flag = flag;
      p.mappings.push_back({q, 6 + static_cast<std::uint32_t>(flag), {action}});
    }
  }
  p.validate();
  return p;
}
} // namespace qsbit
