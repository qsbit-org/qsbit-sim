#include "qsbit/feedback.hpp"
#include <algorithm>
#include <limits>

namespace qsbit {
Scoreboard::Scoreboard(const Profile &profile)
    : profile_(profile), slots_(profile.result_slots), generations_(profile.result_slots, 0) {}
bool Scoreboard::has_slot() const {
  return std::any_of(slots_.begin(), slots_.end(),
                     [](const Slot &s) { return s.state == State::Free; });
}
bool Scoreboard::has_fast_credit() const {
  return !profile_.fast_feedback || fast_pending_.size() < profile_.result_slots;
}
Token Scoreboard::reserve(Epoch epoch, std::uint32_t target) {
  require(target < profile_.qubits, ErrorCode::InvalidOperand, "measurement target is invalid");
  require(has_slot() && has_fast_credit(), ErrorCode::Capacity,
          "measurement reservation capacity exhausted");
  const auto it = std::find_if(slots_.begin(), slots_.end(),
                               [](const Slot &s) { return s.state == State::Free; });
  const auto slot = static_cast<std::uint32_t>(it - slots_.begin());
  const auto generation = checked_add(generations_[slot], 1);
  const auto handle = checked_add(checked_mul(generation - 1, profile_.result_slots), slot + 1);
  require(handle <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::TimeOverflow,
          "measurement handle exhausted");
  const auto following = checked_add(next_measurement_, 1);
  Token result{epoch,
               next_measurement_,
               slot,
               static_cast<std::uint32_t>(generation),
               static_cast<std::uint32_t>(handle),
               target};
  *it = Slot{State::Pending, result, false};
  generations_[slot] = result.generation;
  next_measurement_ = following;
  if (profile_.fast_feedback)
    fast_pending_.emplace(result.measurement, result);
  return result;
}
Token Scoreboard::token(std::uint32_t handle, Epoch epoch) const {
  require(handle > 0, ErrorCode::InvalidToken, "zero measurement handle");
  const auto index = (handle - 1) % profile_.result_slots;
  const auto &s = slots_[index];
  require(s.state != State::Free && s.token.handle == handle && s.token.epoch == epoch,
          ErrorCode::InvalidToken, "unknown, consumed, or stale measurement handle");
  return s.token;
}
void Scoreboard::deliver(const Completion &completion, Epoch epoch) {
  if (completion.token.epoch != epoch)
    return;
  const auto expected = token(completion.token.handle, epoch);
  require(expected == completion.token, ErrorCode::InvalidToken,
          "completion token does not match reserved slot");
  auto &slot = slots_[expected.slot];
  require(slot.state == State::Pending, ErrorCode::DuplicateResult, "result was already delivered");
  slot.value = completion.value;
  slot.state = State::Visible;
}
void Scoreboard::acknowledge_fast(const Token &token_value, Epoch epoch) {
  if (token_value.epoch != epoch)
    return;
  const auto it = fast_pending_.find(token_value.measurement);
  require(it != fast_pending_.end() && it->second == token_value, ErrorCode::InvalidToken,
          "unknown or duplicate fast delivery credit");
  fast_pending_.erase(it);
}
std::optional<bool> Scoreboard::consume(std::uint32_t handle, Epoch epoch) {
  const auto selected = token(handle, epoch);
  auto &slot = slots_[selected.slot];
  if (slot.state == State::Pending)
    return std::nullopt;
  const bool value = slot.value;
  slot.state = State::Free;
  return value;
}
bool Scoreboard::deliveries_pending() const {
  return !fast_pending_.empty() || std::any_of(slots_.begin(), slots_.end(), [](const Slot &slot) {
    return slot.state == State::Pending;
  });
}
void Scoreboard::reset() {
  for (auto &slot : slots_)
    slot = Slot{};
  fast_pending_.clear();
  next_measurement_ = 1;
}
bool FastHistory::evaluate(const Condition &condition) const {
  require(condition.token.target < history_.size(), ErrorCode::InvalidToken,
          "condition target is invalid");
  const auto &history = history_[condition.token.target];
  const auto it = std::find_if(history.begin(), history.end(),
                               [&](const Completion &c) { return c.token == condition.token; });
  require(it != history.end(), ErrorCode::InvalidToken,
          "required fast-condition history is unavailable");
  return it->value == condition.expected;
}
void FastHistory::commit(const Completion &completion, Epoch epoch) {
  if (completion.token.epoch != epoch)
    return;
  require(completion.token.target < history_.size(), ErrorCode::InvalidToken,
          "result target is invalid");
  auto &history = history_[completion.token.target];
  require(history.empty() || (history.back().token.epoch == epoch &&
                              history.back().token.measurement < completion.token.measurement),
          ErrorCode::Protocol, "same-target fast results are duplicated or out of issue order");
  history.push_back(completion);
  if (history.size() > profile_.history_depth)
    history.pop_front();
}
void FastHistory::reset() {
  for (auto &history : history_)
    history.clear();
}
} // namespace qsbit
