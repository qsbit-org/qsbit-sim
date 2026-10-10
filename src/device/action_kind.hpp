#pragma once
#include "qsbit/profile.hpp"
namespace qsbit {
inline bool is_arm(const EventSpec &action) {
  return action.kind() == ActionKind::DiscriminatorArm;
}
inline bool is_gate(const EventSpec &action) {
  return action.kind() == ActionKind::IdealGate || action.kind() == ActionKind::GateOutput;
}
} // namespace qsbit
