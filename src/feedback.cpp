#include "qsbit/feedback.hpp"
#include <algorithm>

namespace qsbit {
MeasurementRegisters::MeasurementRegisters(FeedbackConfig config)
    : config_(config), registers_(config.qubits) {}
bool MeasurementRegisters::has_capacity() const {
  return pending_.size() < config_.result_capacity &&
         (!config_.fast_feedback || fast_pending_.size() < config_.result_capacity);
}
MeasurementReference MeasurementRegisters::reserve(Epoch epoch, std::uint32_t target) {
  require(target < registers_.size(), ErrorCode::InvalidOperand, "measurement target is invalid");
  require(has_capacity(), ErrorCode::Capacity, "measurement delivery capacity exhausted");
  const auto following = checked_add(next_measurement_, 1);
  MeasurementReference result{epoch, next_measurement_, target};
  pending_.emplace(result.measurement, result);
  ++registers_[target].pending;
  next_measurement_ = following;
  if (config_.fast_feedback)
    fast_pending_.emplace(result.measurement, result);
  return result;
}
void MeasurementRegisters::deliver(const Completion &completion, Epoch epoch) {
  if (completion.reference.epoch != epoch)
    return;
  const auto &reference = completion.reference;
  const auto it = pending_.find(reference.measurement);
  require(it != pending_.end(), ErrorCode::DuplicateResult, "measurement is not pending");
  require(it->second == reference, ErrorCode::InvalidMeasurement, "measurement reference mismatch");
  require(std::none_of(pending_.begin(), it,
                       [&](const auto &entry) { return entry.second.target == reference.target; }),
          ErrorCode::Protocol, "measurement results arrived out of order");
  auto &reg = registers_.at(reference.target);
  reg.value = completion.value;
  --reg.pending;
  pending_.erase(it);
}
void MeasurementRegisters::acknowledge_fast(const MeasurementReference &reference, Epoch epoch) {
  if (reference.epoch != epoch)
    return;
  const auto it = fast_pending_.find(reference.measurement);
  require(it != fast_pending_.end() && it->second == reference, ErrorCode::InvalidMeasurement,
          "unknown or duplicate fast delivery acknowledgment");
  fast_pending_.erase(it);
}
std::optional<bool> MeasurementRegisters::read(std::uint32_t target) const {
  require(target < registers_.size(), ErrorCode::InvalidOperand, "measurement register is invalid");
  const auto &reg = registers_[target];
  if (reg.pending != 0)
    return std::nullopt;
  return reg.value;
}
bool MeasurementRegisters::deliveries_pending() const {
  return !pending_.empty() || !fast_pending_.empty();
}
void MeasurementRegisters::reset() {
  std::fill(registers_.begin(), registers_.end(), Register{});
  pending_.clear();
  fast_pending_.clear();
  next_measurement_ = 1;
}
bool ExecutionFlags::evaluate(std::uint32_t target, ExecutionFlag flag) const {
  require(target < registers_.size(), ErrorCode::InvalidOperand,
          "execution flag target is invalid");
  const auto &reg = registers_[target];
  switch (flag) {
  case ExecutionFlag::Always:
    return true;
  case ExecutionFlag::LastOne:
    return reg.last_one;
  case ExecutionFlag::LastZero:
    return reg.last_zero;
  case ExecutionFlag::Equal:
    return reg.equal;
  }
  throw Fault(ErrorCode::InvalidOperand, "execution flag is invalid");
}
void ExecutionFlags::commit(const Completion &completion, Epoch epoch) {
  if (completion.reference.epoch != epoch)
    return;
  require(completion.reference.target < registers_.size(), ErrorCode::InvalidMeasurement,
          "measurement target is invalid");
  auto &reg = registers_[completion.reference.target];
  require(completion.reference.measurement > reg.last_measurement, ErrorCode::Protocol,
          "fast results are duplicated or out of order");
  reg.equal = reg.last_measurement != 0 && reg.last_one == completion.value;
  reg.last_one = completion.value;
  reg.last_zero = !completion.value;
  reg.last_measurement = completion.reference.measurement;
}
void ExecutionFlags::reset() { std::fill(registers_.begin(), registers_.end(), Register{}); }
} // namespace qsbit
