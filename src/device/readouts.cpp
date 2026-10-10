#include "qsbit/device/readouts.hpp"
#include "action_kind.hpp"
#include "qsbit/control_protocol.hpp"
#include "qsbit/error.hpp"
#include "qsbit/event.hpp"
#include "qsbit/measurement.hpp"
#include "qsbit/time.hpp"
#include <algorithm>
#include <map>
#include <span>
#include <vector>

namespace qsbit {
std::vector<Readout> Readouts::prepare(std::span<const ScheduledEvent> actions, Epoch epoch) const {
  std::vector<Readout> result;
  std::map<Id, unsigned> acquisitions, arms;
  std::map<Id, MeasurementReference> identities;
  for (const auto &action : actions) {
    const auto &e = action.event;
    if (e.action.kind() == ActionKind::Acquire || is_arm(e.action)) {
      require(e.reference && e.reference->epoch == epoch, ErrorCode::InvalidMeasurement,
              "readout reference missing or stale");
      require(e.action.targets().size() == 1 && e.action.targets().front() == e.reference->target,
              ErrorCode::InvalidMeasurement, "readout target and reference differ");
      const auto [identity, inserted] = identities.emplace(e.reference->measurement, *e.reference);
      require(inserted || identity->second == *e.reference, ErrorCode::InvalidMeasurement,
              "readout members disagree on reference identity");
      if (is_arm(e.action))
        ++arms[e.reference->measurement];
      else
        ++acquisitions[e.reference->measurement];
    }
  }
  for (const auto &action : actions)
    if (action.event.action.kind() == ActionKind::Acquire) {
      const auto id = action.event.reference->measurement;
      require(acquisitions[id] == 1 && !pending_.contains(id) &&
                  arms[id] == (action.event.action.get<AcquireSpec>().separate_arm ? 1U : 0U),
              ErrorCode::Protocol, "invalid acquisition and arm pairing");
      Tick arm = action.start;
      if (action.event.action.get<AcquireSpec>().separate_arm)
        for (const auto &other : actions)
          if (is_arm(other.event.action) && other.event.reference == action.event.reference)
            arm = other.start;
      const Tick ready = checked_add(std::max(action.end, arm),
                                     action.event.action.get<AcquireSpec>().discriminator_delay);
      result.push_back({*action.event.reference, action.end, arm, ready, {}, action.event.core});
    }
  for (const auto &[id, count] : arms)
    if (count > 0)
      require(acquisitions[id] == 1, ErrorCode::Protocol, "orphan discriminator arm");
  return result;
}
void Readouts::insert(std::span<const Readout> readouts) {
  for (const auto &readout : readouts)
    require(pending_.emplace(readout.reference.measurement, readout).second, ErrorCode::Protocol,
            "duplicate pending readout");
}
void Readouts::sample(Id measurement, bool value) { pending_.at(measurement).sample = value; }
const Readout &Readouts::at(Id measurement) const { return pending_.at(measurement); }
Completion Readouts::result(Id measurement) const {
  const auto &readout = at(measurement);
  require(readout.sample.has_value(), ErrorCode::Protocol, "result is ready before sampling");
  return {readout.reference, *readout.sample};
}
} // namespace qsbit
