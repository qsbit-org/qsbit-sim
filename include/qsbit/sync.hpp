#pragma once

#include "qsbit/trace.hpp"
#include <deque>
#include <map>
#include <optional>
#include <span>

namespace qsbit {
struct SyncConnection {
  std::uint32_t first = 0, second = 0;
  Tick first_to_second = 1, second_to_first = 1;
  std::size_t capacity = 8;
};
// Configured directional delays in TCU cycles between neighboring controllers.
class SyncNetwork {
public:
  SyncNetwork(Clock clock, std::span<const SyncConnection> connections);
  [[nodiscard]] Tick delay(std::uint32_t source, std::uint32_t target) const;
  void preflight(std::uint32_t source, std::uint32_t target, Tick now);
  void send(std::uint32_t source, std::uint32_t target, Tick now);
  std::optional<Tick> receive(std::uint32_t source, std::uint32_t target, Tick now);
  [[nodiscard]] bool empty() const;
  void reset();

private:
  struct Signal {
    Tick arrival;
    std::optional<Tick> consumed;
  };
  struct Link {
    Tick delay;
    std::size_t capacity;
    std::deque<Signal> signals;
  };
  Clock clock_;
  std::map<std::pair<std::uint32_t, std::uint32_t>, Link> links_;
};
class SyncUnit {
public:
  SyncUnit(std::uint32_t core, SyncNetwork &network, Trace &trace);
  void validate(std::uint32_t target) const;
  void preflight(std::span<const std::uint32_t> targets, Tick now);
  void book(std::span<const std::uint32_t> targets, Tick now, Epoch epoch);
  // Runs before TCU event triggering on each TCU edge.
  bool step(Tick now, Epoch epoch);
  [[nodiscard]] bool drained() const { return !pending_; }
  void reset();

private:
  struct Booking {
    std::uint32_t target;
    Tick deadline;
    bool received = false;
  };
  std::uint32_t core_;
  SyncNetwork &network_;
  Trace &trace_;
  std::optional<Booking> pending_;
  bool paused_ = false;
};
} // namespace qsbit
