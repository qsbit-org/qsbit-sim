#pragma once

#include "qsbit/backend.hpp"
#include <memory>
#include <string>

namespace qsbit {
class PythonSession {
public:
  explicit PythonSession(const std::string &module_directory);
  ~PythonSession();
  PythonSession(const PythonSession &) = delete;
  PythonSession &operator=(const PythonSession &) = delete;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
class PythonBackend final : public IQuantumBackend {
public:
  PythonBackend(const std::string &module, const std::string &class_name);
  ~PythonBackend() override;
  void validate(const ActionSpec &action) const override;
  void reset(std::uint32_t qubits, std::uint32_t seed) override;
  void evolve(Tick from, Tick to, std::span<const ActionSpec> active_drives) override;
  void apply(std::span<const ActionSpec> gates) override;
  std::vector<bool> measure(std::span<const Token> tokens) override;
  [[nodiscard]] std::vector<std::complex<double>> state() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace qsbit
