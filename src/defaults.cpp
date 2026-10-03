#include "qsbit/defaults.hpp"
#include <numbers>

namespace qsbit {
Profile default_profile() {
  Profile p;
  for (std::uint32_t q = 0; q < p.qubits; ++q) {
    for (std::uint32_t code = 1; code <= 5; ++code) {
      EventSpec a;
      a.port = q;
      a.targets = {q};
      a.resources = {{q, true}};
      if (code == 1)
        a.operation = "x";
      if (code == 2)
        a.operation = "h";
      if (code == 3)
        a.operation = "z";
      if (code == 4) {
        a.operation = "measure";
        a.kind = ActionKind::Acquire;
        a.duration = 40;
      }
      if (code == 5) {
        a.operation = "drive_x";
        a.kind = ActionKind::Pulse;
        a.amplitude = std::numbers::pi / 20.0;
      }
      p.mappings.push_back({q, code, {a}});
    }
  }
  p.two_qubit_gates.push_back(
      {"cx01", "cx", {0, 1}, {{0, 0, 6}, {0, 1, 10}}, {{0, true}, {1, true}}, 20});
  for (auto [port, code] : {std::pair{0U, 6U}, std::pair{1U, 10U}}) {
    EventSpec output;
    output.kind = ActionKind::GateOutput;
    output.port = port;
    output.operation.clear();
    output.gate = "cx01";
    p.mappings.push_back({port, code, {output}});
  }
  for (std::uint32_t q = 0; q < p.qubits; ++q) {
    for (auto flag : {ExecutionFlag::LastOne, ExecutionFlag::LastZero, ExecutionFlag::Equal}) {
      EventSpec action;
      action.port = q;
      action.targets = {q};
      action.resources = {{q, true}};
      action.execution_flag = flag;
      p.mappings.push_back({q, 6 + static_cast<std::uint32_t>(flag), {action}});
    }
  }
  p.validate();
  return p;
}
} // namespace qsbit
