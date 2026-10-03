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
struct PythonBackendConfig {
  std::string name;
  std::string options = "{}";
};
class PythonBackend final : public IQuantumBackend {
public:
  explicit PythonBackend(const PythonBackendConfig &config);
  static std::string inspect(const std::string &name, const std::string &command);
  static void run_simulation(const std::string &config, const std::string &executable,
                             bool check_only = false);
  [[nodiscard]] std::string options() const;
  ~PythonBackend() override;
  void validate(const EventSpec &action) const override;
  void reset(std::uint32_t qubits, std::uint32_t seed) override;
  std::vector<bool> execute(Epoch epoch, std::span<const BackendOperation> operations) override;
  [[nodiscard]] std::vector<std::complex<double>> state() const override;
  [[nodiscard]] std::vector<std::vector<std::complex<double>>> density_matrix() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace qsbit
