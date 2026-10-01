#pragma once

#include "qsbit/control.hpp"
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace qsbit {
class MeasurementResults {
public:
  enum class State { Free, Pending, Visible };
  struct Slot {
    State state = State::Free;
    MeasurementReference token;
    bool value = false;
  };
  explicit MeasurementResults(const Profile &profile);
  [[nodiscard]] bool has_slot() const;
  [[nodiscard]] bool has_fast_credit() const;
  MeasurementReference reserve(Epoch epoch, std::uint32_t target);
  [[nodiscard]] MeasurementReference token(std::uint32_t handle, Epoch epoch) const;
  void deliver(const Completion &completion, Epoch epoch);
  void acknowledge_fast(const MeasurementReference &token, Epoch epoch);
  std::optional<bool> consume(std::uint32_t handle, Epoch epoch);
  [[nodiscard]] bool deliveries_pending() const;
  [[nodiscard]] const std::vector<Slot> &slots() const { return slots_; }
  void reset();

private:
  const Profile &profile_;
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> generations_;
  std::map<Id, MeasurementReference> fast_pending_;
  Id next_measurement_ = 1;
};
// Measurement results retained for the simulator's exact-reference conditions.
// These are not eQASM's derived execution-flag registers.
class ConditionalResults {
public:
  explicit ConditionalResults(const Profile &profile)
      : profile_(profile), history_(profile.qubits) {}
  [[nodiscard]] bool evaluate(const Condition &condition) const;
  void commit(const Completion &completion, Epoch epoch);
  void reset();

private:
  const Profile &profile_;
  std::vector<std::deque<Completion>> history_;
};
using Scoreboard = MeasurementResults;
using FastHistory = ConditionalResults;
} // namespace qsbit
