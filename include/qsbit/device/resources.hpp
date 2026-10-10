#pragma once
#include "qsbit/control_protocol.hpp"
#include <span>
namespace qsbit {
// Simulator conflict checks for configured output intervals.
class ResourceReservations {
public:
  void check(std::span<const ScheduledEvent> actions) const;
  void reserve(std::span<const ScheduledEvent> actions);
  void discard_before(Tick now);
  void reset() { reservations_.clear(); }
  [[nodiscard]] const std::vector<ScheduledEvent> &reservations() const { return reservations_; }

private:
  static void pair(const ScheduledEvent &a, const ScheduledEvent &b);
  std::vector<ScheduledEvent> reservations_;
};
} // namespace qsbit
