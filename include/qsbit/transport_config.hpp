#pragma once

#include "qsbit/time.hpp"
#include <cstdint>

namespace qsbit {
struct Profile;
struct TransportConfig {
  Clock cpu, tcu;
  std::uint32_t command_latency, reply_latency, cpu_result_latency, fast_result_latency;
  std::uint32_t result_capacity;
};
[[nodiscard]] TransportConfig transport_config(const Profile &profile);
} // namespace qsbit
