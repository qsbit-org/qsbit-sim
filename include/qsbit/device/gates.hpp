#pragma once

#include "qsbit/control_protocol.hpp"
#include <map>
#include <string>
#include <vector>

namespace qsbit {
struct Profile;
struct DeviceProfile {
  std::uint32_t qubits, seed;
  std::map<std::string, TwoQubitGate> gates;
  [[nodiscard]] const TwoQubitGate &gate(const std::string &name) const;
};
[[nodiscard]] DeviceProfile device_profile(const Profile &profile);
[[nodiscard]] EventSpec gate_action(const DeviceProfile &profile, const std::string &name);
[[nodiscard]] std::vector<EventSpec>
resolve_gate_outputs(const DeviceProfile &profile, Tick now,
                     const std::map<std::string, std::vector<const ScheduledEvent *>> &outputs);
} // namespace qsbit
