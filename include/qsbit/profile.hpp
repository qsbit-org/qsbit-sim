#pragma once

#include "qsbit/time.hpp"
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace qsbit {
enum class ActionKind { IdealGate, Pulse, Acquire, DiscriminatorArm, GateOutput };
enum class ExecutionFlag { Always, LastOne, LastZero, Equal };
struct ResourceUse {
  std::uint32_t id = 0;
  bool exclusive = true;
  bool operator==(const ResourceUse &) const = default;
};
struct QuantumOperands {
  std::vector<std::uint32_t> targets;
  std::vector<ResourceUse> resources;
  bool operator==(const QuantumOperands &) const = default;
};
struct GateSpec {
  std::string operation = "x";
  QuantumOperands operands;
  double amplitude = 0.0;
  ExecutionFlag execution_flag = ExecutionFlag::Always;
  bool operator==(const GateSpec &) const = default;
};
struct PulseSpec {
  std::string operation = "x";
  QuantumOperands operands;
  double amplitude = 0.0;
  std::string axis = "x";
  ExecutionFlag execution_flag = ExecutionFlag::Always;
  bool operator==(const PulseSpec &) const = default;
};
struct AcquireSpec {
  std::string operation = "x";
  QuantumOperands operands;
  Tick discriminator_delay = 20;
  bool separate_arm = false;
  bool operator==(const AcquireSpec &) const = default;
};
struct ArmSpec {
  std::string operation = "x";
  QuantumOperands operands;
  bool operator==(const ArmSpec &) const = default;
};
struct GateOutputSpec {
  std::string gate;
  bool operator==(const GateOutputSpec &) const = default;
};
struct EventSpec {
  std::uint32_t port = 0;
  Tick delay = 0, duration = 20;
  std::variant<GateSpec, PulseSpec, AcquireSpec, ArmSpec, GateOutputSpec> spec = GateSpec{};
  template <typename T> T &get() { return std::get<T>(spec); }
  template <typename T> const T &get() const { return std::get<T>(spec); }
  [[nodiscard]] ActionKind kind() const;
  [[nodiscard]] const std::vector<std::uint32_t> &targets() const;
  [[nodiscard]] const std::vector<ResourceUse> &resources() const;
  [[nodiscard]] std::string operation() const;
  [[nodiscard]] ExecutionFlag execution_flag() const;
  bool operator==(const EventSpec &) const = default;
};
struct Mapping {
  std::uint32_t port = 0, codeword = 0;
  std::vector<EventSpec> actions;
};
struct GateInput {
  std::uint32_t core = 0, port = 0, codeword = 0;
  bool operator==(const GateInput &) const = default;
};
struct TwoQubitGate {
  std::string name, operation;
  std::vector<std::uint32_t> targets;
  std::vector<GateInput> inputs;
  std::vector<ResourceUse> resources;
  Tick duration = 20;
  bool operator==(const TwoQubitGate &) const = default;
};
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
