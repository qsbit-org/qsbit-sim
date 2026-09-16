#pragma once

#include "qsbit/control.hpp"
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace qsbit {
class Scoreboard {
public:
  enum class State { Free, Pending, Visible };
  struct Slot {
    State state = State::Free;
    Token token;
    bool value = false;
  };
  explicit Scoreboard(const Profile &profile);
  [[nodiscard]] bool has_slot() const;
  [[nodiscard]] bool has_fast_credit() const;
  Token reserve(Epoch epoch, std::uint32_t target);
  [[nodiscard]] Token token(std::uint32_t handle, Epoch epoch) const;
  void deliver(const Completion &completion, Epoch epoch);
  void acknowledge_fast(const Token &token, Epoch epoch);
  std::optional<bool> consume(std::uint32_t handle, Epoch epoch);
  [[nodiscard]] bool deliveries_pending() const;
  [[nodiscard]] const std::vector<Slot> &slots() const { return slots_; }
  void reset();

private:
  const Profile &profile_;
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> generations_;
  std::map<Id, Token> fast_pending_;
  Id next_measurement_ = 1;
};
class FastHistory {
public:
  explicit FastHistory(const Profile &profile) : profile_(profile), history_(profile.qubits) {}
  [[nodiscard]] bool evaluate(const Condition &condition) const;
  void commit(const Completion &completion, Epoch epoch);
  void reset();

private:
  const Profile &profile_;
  std::vector<std::deque<Completion>> history_;
};
} // namespace qsbit
