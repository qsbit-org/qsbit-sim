#include "qsbit/feedback.hpp"
#include <algorithm>
#include <limits>

namespace qsbit {
MeasurementRegisters::MeasurementRegisters(FeedbackConfig config)
    : config_(config), registers_(config.qubits) {}
bool MeasurementRegisters::has_capacity(std::size_t count) const {
  return count <= config_.result_capacity - pending_.size() &&
         (!config_.fast_feedback || count <= config_.result_capacity - fast_pending_.size());
}
std::vector<MeasurementReference>
MeasurementRegisters::prepare_reservation(Epoch epoch,
                                          std::span<const std::uint32_t> targets) const {
  require(has_capacity(targets.size()), ErrorCode::Capacity,
          "measurement delivery capacity exhausted");
  (void)checked_add(next_measurement_, targets.size());
  std::vector<MeasurementReference> references;
  for (auto target : targets) {
    require(target < registers_.size(), ErrorCode::InvalidOperand, "measurement target is invalid");
    const auto count = std::count(targets.begin(), targets.end(), target);
    require(static_cast<std::uint64_t>(count) <=
                std::numeric_limits<std::uint32_t>::max() - registers_[target].pending,
            ErrorCode::TimeOverflow, "pending measurement count overflow");
    references.push_back({epoch, checked_add(next_measurement_, references.size()), target});
  }
  return references;
}
void MeasurementRegisters::commit_reservation(std::span<const MeasurementReference> references) {
  if (references.empty())
    return;
  std::vector<std::uint32_t> targets;
  for (const auto &reference : references)
    targets.push_back(reference.target);
  const auto expected = prepare_reservation(references.front().epoch, targets);
  require(std::equal(expected.begin(), expected.end(), references.begin()), ErrorCode::Protocol,
          "stale measurement reservation");
  // Build the entire reservation before changing any owned state.
  auto pending = pending_;
  auto fast_pending = fast_pending_;
  for (const auto &reference : references) {
    pending.emplace(reference.measurement, reference);
    if (config_.fast_feedback)
      fast_pending.emplace(reference.measurement, reference);
  }
  pending_.swap(pending);
  fast_pending_.swap(fast_pending);
  for (auto target : targets)
    ++registers_[target].pending;
  next_measurement_ = checked_add(next_measurement_, references.size());
}
MeasurementReference MeasurementRegisters::reserve(Epoch epoch, std::uint32_t target) {
  const auto references = prepare_reservation(epoch, std::span{&target, 1});
  commit_reservation(references);
  return references.front();
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
