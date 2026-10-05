#pragma once

#include "qsbit/control.hpp"
#include <complex>
#include <span>
#include <vector>

namespace qsbit {
struct BackendActivity {
  Id id;
  Tick start, end;
  EventSpec action;
  std::optional<MeasurementReference> reference;
};
struct BackendEvolution {
  Tick start, tick;
  std::vector<BackendActivity> drives;
  std::vector<BackendActivity> acquisitions;
};
struct BackendGates {
  Tick tick;
  std::vector<EventSpec> gates;
};
struct BackendMeasurement {
  Tick tick;
  std::vector<MeasurementReference> references;
};
using BackendOperation = std::variant<BackendEvolution, BackendGates, BackendMeasurement>;
struct BackendExecutionConfig {
  std::uint32_t max_batch_operations = 1024;
};
class IQuantumBackend {
public:
  virtual ~IQuantumBackend() = default;
  virtual void validate(const EventSpec &action) const = 0;
  virtual void reset(std::uint32_t qubits, std::uint32_t seed) = 0;
  virtual std::vector<bool> execute(Epoch epoch, std::span<const BackendOperation> operations) = 0;
  [[nodiscard]] virtual std::vector<std::complex<double>> state() const { return {}; }
  [[nodiscard]] virtual std::vector<std::vector<std::complex<double>>> density_matrix() const {
    return {};
  }
};
class BackendExecution {
public:
  explicit BackendExecution(IQuantumBackend &backend, BackendExecutionConfig config = {});
  void validate(const EventSpec &action) const { backend_.validate(action); }
  void reset(std::uint32_t qubits, std::uint32_t seed, Epoch epoch, Tick now = 0);
  void evolve(Tick from, Tick to, std::span<const BackendActivity> active_drives,
              std::span<const BackendActivity> acquisitions = {});
  void apply(Tick now, std::span<const EventSpec> gates);
  std::vector<bool> measure(Tick now, std::span<const MeasurementReference> references);
  void flush();
  [[nodiscard]] std::vector<std::complex<double>> state();
  [[nodiscard]] std::vector<std::vector<std::complex<double>>> density_matrix();

private:
  void commit(BackendOperation operation);
  std::vector<bool> execute(std::size_t expected_results);
  IQuantumBackend &backend_;
  BackendExecutionConfig config_;
  std::vector<BackendOperation> pending_;
  Epoch epoch_ = 0;
  Tick committed_tick_ = 0;
};
class MockBackend final : public IQuantumBackend {
public:
  explicit MockBackend(std::map<Id, bool> outcomes = {}) : outcomes_(std::move(outcomes)) {}
  void validate(const EventSpec &action) const override;
  void reset(std::uint32_t qubits, std::uint32_t seed) override;
  std::vector<bool> execute(Epoch epoch, std::span<const BackendOperation> operations) override;
  [[nodiscard]] std::vector<std::complex<double>> state() const override { return {}; }
  [[nodiscard]] std::size_t calls() const { return calls_; }

private:
  std::map<Id, bool> outcomes_;
  std::uint32_t qubits_ = 0;
  std::size_t calls_ = 0;
};
} // namespace qsbit
