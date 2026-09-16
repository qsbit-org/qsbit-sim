#pragma once

#include "qsbit/time.hpp"
#include <deque>
#include <optional>
#include <utility>

namespace qsbit {
// Durable immutable envelopes. Polling on the publication tick cannot expose a value.
template <typename T> class Mailbox {
public:
  struct Envelope {
    Tick published;
    Tick eligible;
    Epoch epoch;
    T value;
  };
  Mailbox(std::size_t capacity, Clock receiver, std::uint32_t latency = 1)
      : capacity_(capacity), receiver_(receiver), latency_(latency) {
    require(capacity > 0, ErrorCode::InvalidProfile, "zero mailbox capacity");
    receiver_.validate();
    require(latency > 0, ErrorCode::InvalidProfile, "zero mailbox latency");
  }
  [[nodiscard]] bool full() const { return entries_.size() == capacity_; }
  [[nodiscard]] bool empty() const { return entries_.empty(); }
  [[nodiscard]] std::size_t size() const { return entries_.size(); }
  void publish(Tick now, Epoch epoch, T value) {
    require(!full(), ErrorCode::Capacity, "mailbox capacity exceeded");
    const Tick eligible = receiver_.after(now, latency_);
    require(entries_.empty() || now >= entries_.back().published, ErrorCode::Protocol,
            "mailbox publication time decreased");
    entries_.push_back({now, eligible, epoch, std::move(value)});
  }
  [[nodiscard]] const Envelope *peek(Tick now) const {
    return !entries_.empty() && entries_.front().eligible <= now ? &entries_.front() : nullptr;
  }
  std::optional<Envelope> take(Tick now) {
    if (peek(now) == nullptr)
      return std::nullopt;
    auto value = std::move(entries_.front());
    entries_.pop_front();
    return value;
  }
  void reset() { entries_.clear(); }

private:
  std::size_t capacity_;
  Clock receiver_;
  std::uint32_t latency_;
  std::deque<Envelope> entries_;
};
} // namespace qsbit
