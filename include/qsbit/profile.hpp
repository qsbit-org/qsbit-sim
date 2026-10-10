#pragma once

#include "qsbit/event.hpp"
#include "qsbit/time.hpp"
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace qsbit {
struct Profile {
  Clock cpu{5, 0}, tcu{20, 0};
  Tick start = 1000, watchdog = 1000000;
  std::uint32_t memory_latency = 1, command_latency = 1, reply_latency = 1;
  std::uint32_t cpu_result_latency = 1, fast_result_latency = 2;
  std::uint32_t timing_capacity = 32, event_capacity = 32, staging_capacity = 16;
  std::uint32_t result_capacity = 8, ports = 8, qubits = 2;
  std::uint32_t firing_width = 1, seed = 1;
  bool fast_feedback = true;
  std::vector<Mapping> mappings;
  std::vector<TwoQubitGate> two_qubit_gates;
  void validate() const;
  [[nodiscard]] const Mapping &mapping(std::uint32_t port, std::uint32_t codeword) const;
  [[nodiscard]] const TwoQubitGate &gate(const std::string &name) const;
  [[nodiscard]] std::string fingerprint() const;
};
} // namespace qsbit
