#include "qsbit/device/resources.hpp"
#include "action_kind.hpp"
#include <algorithm>
#include <set>
namespace qsbit {
namespace {
bool common_target(const EventSpec &a, const EventSpec &b) {
  return std::any_of(a.targets().begin(), a.targets().end(), [&](auto q) {
    return std::find(b.targets().begin(), b.targets().end(), q) != b.targets().end();
  });
}
} // namespace
void ResourceReservations::pair(const ScheduledEvent &a, const ScheduledEvent &b) {
  const auto &x = a.resolved;
  const auto &y = b.resolved;
  const bool overlap = a.start < b.end && b.start < a.end;
  if (overlap) {
    require(x.port != y.port, ErrorCode::ResourceConflict, "output port intervals overlap");
    if (a.event.action.kind() == ActionKind::GateOutput &&
        b.event.action.kind() == ActionKind::GateOutput &&
        a.event.action.get<GateOutputSpec>().gate == b.event.action.get<GateOutputSpec>().gate) {
      require(a.start == b.start && a.end == b.end, ErrorCode::GateInputMismatch,
              "two-qubit gate output intervals differ");
      return;
    }
    for (const auto &rx : x.resources())
      for (const auto &ry : y.resources())
        require(rx.id != ry.id || (!rx.exclusive && !ry.exclusive), ErrorCode::ResourceConflict,
                "exclusive resource intervals overlap");
    if (!is_arm(x) && !is_arm(y) && common_target(x, y))
      require(x.kind() == ActionKind::Pulse && y.kind() == ActionKind::Pulse,
              ErrorCode::ResourceConflict, "incompatible quantum actions overlap on one target");
  }
  if (common_target(x, y)) {
    const bool measurement_gate =
        (x.kind() == ActionKind::Acquire && is_gate(y) && a.end == b.start) ||
        (y.kind() == ActionKind::Acquire && is_gate(x) && b.end == a.start);
    require(!measurement_gate, ErrorCode::ResourceConflict,
            "measurement sample and ideal gate share a target and tick");
  }
}
void ResourceReservations::check(std::span<const ScheduledEvent> actions) const {
  std::set<Id> ids;
  for (std::size_t i = 0; i < actions.size(); ++i) {
    const auto &action = actions[i];
    require(ids.insert(action.event.id).second, ErrorCode::Protocol, "duplicate action in launch");
    for (const auto &old : reservations_) {
      require(old.event.id != action.event.id, ErrorCode::Protocol, "action was already reserved");
      pair(action, old);
    }
    for (std::size_t j = 0; j < i; ++j)
      pair(action, actions[j]);
  }
}
void ResourceReservations::reserve(std::span<const ScheduledEvent> actions) {
  check(actions);
  reservations_.insert(reservations_.end(), actions.begin(), actions.end());
}
void ResourceReservations::discard_before(Tick now) {
  std::erase_if(reservations_, [&](const ScheduledEvent &action) { return action.end < now; });
}
} // namespace qsbit
