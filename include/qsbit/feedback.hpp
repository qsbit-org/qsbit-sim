#pragma once

#include "qsbit/control_protocol.hpp"
#include <map>
#include <optional>
#include <vector>

namespace qsbit {
class MeasurementRegisters {
public:
  struct Register {
    std::uint32_t pending = 0;
    bool value = false;
  };
  explicit MeasurementRegisters(const Profile &profile);
  [[nodiscard]] bool has_capacity() const;
  MeasurementReference reserve(Epoch epoch, std::uint32_t target);
  void deliver(const Completion &completion, Epoch epoch);
  void acknowledge_fast(const MeasurementReference &reference, Epoch epoch);
  [[nodiscard]] std::optional<bool> read(std::uint32_t target) const;
  [[nodiscard]] bool deliveries_pending() const;
  [[nodiscard]] const std::vector<Register> &registers() const { return registers_; }
  void reset();

private:
  const Profile &profile_;
  std::vector<Register> registers_;
  std::map<Id, MeasurementReference> pending_, fast_pending_;
  Id next_measurement_ = 1;
};
class ExecutionFlags {
public:
  explicit ExecutionFlags(const Profile &profile) : registers_(profile.qubits) {}
  [[nodiscard]] bool evaluate(std::uint32_t target, ExecutionFlag flag) const;
  void commit(const Completion &completion, Epoch epoch);
  void reset();

private:
  struct Register {
    Id last_measurement = 0;
    bool last_one = false, last_zero = false, equal = false;
  };
  std::vector<Register> registers_;
};
} // namespace qsbit
