#include "qsbit/sync.hpp"
#include <algorithm>
#include <tuple>

namespace qsbit {
SyncNetwork::SyncNetwork(Clock clock, std::span<const SyncConnection> connections) : clock_(clock) {
  clock_.validate();
  for (const auto &c : connections) {
    require(c.first != c.second && c.capacity > 0 && c.first_to_second > 0 && c.second_to_first > 0,
            ErrorCode::InvalidProfile, "invalid synchronization connection");
    for (const auto &[source, target, cycles] :
         {std::tuple{c.first, c.second, c.first_to_second},
          std::tuple{c.second, c.first, c.second_to_first}}) {
      require(links_
                  .emplace(std::pair{source, target},
                           Link{checked_mul(cycles, clock_.period), c.capacity, {}})
                  .second,
              ErrorCode::InvalidProfile, "duplicate synchronization connection");
    }
  }
}
Tick SyncNetwork::delay(std::uint32_t source, std::uint32_t target) const {
  const auto it = links_.find({source, target});
  require(it != links_.end(), ErrorCode::UnsupportedSynchronization,
          "sync target is not a connected neighbor");
  return it->second.delay;
}
void SyncNetwork::preflight(std::uint32_t source, std::uint32_t target, Tick now) {
  (void)delay(source, target);
  require(clock_.edge(now), ErrorCode::Protocol, "sync signal sent off-edge");
  auto &link = links_.at({source, target});
  std::erase_if(link.signals, [now](const Signal &s) { return s.consumed && *s.consumed < now; });
  require(link.signals.size() < link.capacity, ErrorCode::Capacity,
          "synchronization link capacity exceeded");
  (void)checked_add(now, link.delay);
}
void SyncNetwork::send(std::uint32_t source, std::uint32_t target, Tick now) {
  preflight(source, target, now);
  auto &link = links_.at({source, target});
  link.signals.push_back({checked_add(now, link.delay), {}});
}
std::optional<Tick> SyncNetwork::receive(std::uint32_t source, std::uint32_t target, Tick now) {
  auto &link = links_.at({source, target});
  for (auto &s : link.signals)
    if (!s.consumed && s.arrival <= now) {
      s.consumed = now;
      return s.arrival;
    }
  return {};
}
bool SyncNetwork::empty() const {
  return std::all_of(links_.begin(), links_.end(), [](const auto &entry) {
    return std::all_of(entry.second.signals.begin(), entry.second.signals.end(),
                       [](const Signal &s) { return s.consumed.has_value(); });
  });
}
void SyncNetwork::reset() {
  for (auto &[key, link] : links_) {
    (void)key;
    link.signals.clear();
  }
}
SyncUnit::SyncUnit(std::uint32_t core, SyncNetwork &network, Trace &trace)
    : core_(core), network_(network), trace_(trace) {}
void SyncUnit::validate(std::uint32_t target) const { (void)network_.delay(core_, target); }
void SyncUnit::preflight(std::span<const std::uint32_t> targets, Tick now) {
  require(targets.size() <= 1 && (targets.empty() || !pending_),
          ErrorCode::UnsupportedSynchronization, "overlapping sync bookings are unsupported");
  for (auto target : targets)
    network_.preflight(core_, target, now);
}
void SyncUnit::book(std::span<const std::uint32_t> targets, Tick now, Epoch epoch) {
  preflight(targets, now);
  for (auto target : targets) {
    const auto deadline = checked_add(now, network_.delay(core_, target));
    network_.send(core_, target, now);
    pending_ = Booking{target, deadline};
    TraceEvent event{now, epoch, "SyncBooked"};
    event.value = deadline;
    event.targets = {target};
    trace_.emit(std::move(event));
  }
}
bool SyncUnit::step(Tick now, Epoch epoch) {
  bool paused = false;
  if (pending_) {
    if (!pending_->received) {
      if (const auto arrival = network_.receive(pending_->target, core_, now)) {
        pending_->received = true;
        TraceEvent event{now, epoch, "SyncReceived"};
        event.value = *arrival;
        event.targets = {pending_->target};
        trace_.emit(std::move(event));
      }
    }
    if (now >= pending_->deadline && pending_->received) {
      TraceEvent event{now, epoch, "SyncCompleted"};
      event.targets = {pending_->target};
      trace_.emit(std::move(event));
      pending_.reset();
    } else {
      paused = now >= pending_->deadline;
    }
  }
  if (paused != paused_)
    trace_.emit({now, epoch, paused ? "TimerPaused" : "TimerResumed"});
  paused_ = paused;
  return paused;
}
void SyncUnit::reset() {
  pending_.reset();
  paused_ = false;
}
} // namespace qsbit
