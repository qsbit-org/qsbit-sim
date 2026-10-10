#pragma once

#include "qsbit/event.hpp"
#include "qsbit/time.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace qsbit {
struct Profile;
struct TimingConfig {
  std::uint32_t event_capacity, staging_capacity, ports, qubits, firing_width;
  bool fast_feedback;
  std::string configuration;
  std::map<std::pair<std::uint32_t, std::uint32_t>, Mapping> mappings;
  [[nodiscard]] const Mapping &mapping(std::uint32_t port, std::uint32_t codeword) const;
};
[[nodiscard]] TimingConfig timing_config(const Profile &profile);
struct TcuConfig {
  TimingConfig timing;
  Clock clock;
  Tick start;
  std::uint32_t timing_capacity;
};
[[nodiscard]] TcuConfig tcu_config(const Profile &profile);
} // namespace qsbit
