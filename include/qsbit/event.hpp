#pragma once

#include "qsbit/measurement.hpp"
#include <string>
#include <variant>
#include <vector>

namespace qsbit {
enum class ActionKind { IdealGate, Pulse, Acquire, DiscriminatorArm, GateOutput };
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
struct DiscriminatorArmSpec {
  std::string operation = "x";
  QuantumOperands operands;
  bool operator==(const DiscriminatorArmSpec &) const = default;
};
struct GateOutputSpec {
  std::string gate;
  bool operator==(const GateOutputSpec &) const = default;
};
struct EventSpec {
  std::uint32_t port = 0;
  Tick delay = 0, duration = 20;
  std::variant<GateSpec, PulseSpec, AcquireSpec, DiscriminatorArmSpec, GateOutputSpec> spec =
      GateSpec{};
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
} // namespace qsbit
