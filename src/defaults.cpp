#include "qsbit/defaults.hpp"
#include <numbers>

namespace qsbit {
Profile default_profile() {
  Profile p;
  for (std::uint32_t q = 0; q < p.qubits; ++q) {
    for (std::uint32_t code = 1; code <= 5; ++code) {
      ActionSpec a;
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
  ActionSpec cx;
  cx.port = 0;
  cx.operation = "cx";
  cx.targets = {0, 1};
  cx.resources = {{0, true}, {1, true}};
  p.mappings.push_back({0, 6, {cx}});
  p.validate();
  return p;
}
} // namespace qsbit
