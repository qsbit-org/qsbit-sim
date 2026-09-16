#pragma once

#include "qsbit/control.hpp"
#include <complex>
#include <span>
#include <vector>

namespace qsbit {
class IQuantumBackend {
public:
  virtual ~IQuantumBackend() = default;
  virtual void validate(const ActionSpec &action) const = 0;
  virtual void reset(std::uint32_t qubits, std::uint32_t seed) = 0;
  virtual void evolve(Tick from, Tick to, std::span<const ActionSpec> active_drives) = 0;
  virtual void apply(std::span<const ActionSpec> gates) = 0;
  virtual std::vector<bool> measure(std::span<const Token> tokens) = 0;
  [[nodiscard]] virtual std::vector<std::complex<double>> state() const = 0;
};
class ScriptedBackend final : public IQuantumBackend {
public:
  explicit ScriptedBackend(std::map<Id, bool> outcomes = {}) : outcomes_(std::move(outcomes)) {}
  void validate(const ActionSpec &action) const override;
  void reset(std::uint32_t qubits, std::uint32_t seed) override;
  void evolve(Tick from, Tick to, std::span<const ActionSpec> active_drives) override;
  void apply(std::span<const ActionSpec> gates) override;
  std::vector<bool> measure(std::span<const Token> tokens) override;
  [[nodiscard]] std::vector<std::complex<double>> state() const override { return {}; }
  [[nodiscard]] std::size_t calls() const { return calls_; }

private:
  std::map<Id, bool> outcomes_;
  std::uint32_t qubits_ = 0;
  std::size_t calls_ = 0;
};
} // namespace qsbit
