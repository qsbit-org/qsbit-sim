#include "qsbit/control_protocol.hpp"
#include <algorithm>
#include <set>

namespace qsbit {
std::vector<OperationEvent> decode_codeword(const Profile &profile, std::uint32_t port,
                                            std::uint32_t codeword, Epoch epoch, Id instruction,
                                            Id first_event,
                                            std::optional<MeasurementReference> reference) {
  const auto &map = profile.mapping(port, codeword);
  std::vector<OperationEvent> events;
  for (const auto &action : map.actions) {
    const bool readout =
        action.kind() == ActionKind::Acquire || action.kind() == ActionKind::DiscriminatorArm;
    require(!readout || reference.has_value(), ErrorCode::InvalidMeasurement,
            "readout has no reserved reference");
    require(action.execution_flag() == ExecutionFlag::Always || profile.fast_feedback,
            ErrorCode::UnsupportedCapability, "fast feedback is disabled");
    OperationEvent event{
        epoch,  checked_add(first_event, events.size()), instruction, 0, port, codeword,
        action, readout ? reference : std::nullopt};
    events.push_back(std::move(event));
  }
  return events;
}
void validate_timing_events(const TimingEvents &request, const Profile &profile) {
  require(request.point.underflow_policy == UnderflowPolicy::Inherit ||
              request.point.underflow_policy == UnderflowPolicy::Strict ||
              request.point.underflow_policy == UnderflowPolicy::PauseWhenEmpty,
          ErrorCode::Protocol, "invalid timing-point underflow policy");
  require(request.point.interval == 0 || request.point.underflow_policy != UnderflowPolicy::Inherit,
          ErrorCode::Protocol, "positive intervals require an explicit underflow policy");
  require(request.configuration == profile.fingerprint(), ErrorCode::Protocol,
          "request profile mismatch");
  require(request.events.size() == request.point.manifest.size(), ErrorCode::ManifestMismatch,
          "manifest count differs from event count");
  require(request.events.size() <= profile.staging_capacity, ErrorCode::Capacity,
          "oversized staged request");
  require(request.point.synchronizations.size() <= profile.staging_capacity, ErrorCode::Capacity,
          "oversized synchronization request");
  require(request.point.synchronizations.size() <= 1, ErrorCode::InvalidOperand,
          "only one sync is allowed at a time point");
  std::set<std::uint32_t> sync_targets;
  for (auto target : request.point.synchronizations)
    require(target <= 131071 && sync_targets.insert(target).second, ErrorCode::InvalidOperand,
            "invalid or duplicate synchronization target");
  std::set<Id> ids;
  std::vector<std::uint32_t> counts(profile.ports, 0);
  for (std::size_t i = 0; i < request.events.size(); ++i) {
    const auto &e = request.events[i];
    require(e.epoch == request.point.epoch && e.label == request.point.label && e.id != 0 &&
                e.id == request.point.manifest[i] && ids.insert(e.id).second,
            ErrorCode::ManifestMismatch, "invalid manifested event identity");
    require(e.action.port < profile.ports, ErrorCode::InvalidPort,
            "resolved output port is invalid");
    ++counts[e.action.port];
    require(counts[e.action.port] <= profile.firing_width &&
                counts[e.action.port] <= profile.event_capacity,
            ErrorCode::Capacity, "request exceeds per-port firing or storage capacity");
    const auto &map = profile.mapping(e.source_port, e.codeword);
    require(std::find(map.actions.begin(), map.actions.end(), e.action) != map.actions.end(),
            ErrorCode::Protocol, "resolved descriptor differs from immutable mapping");
  }
}
} // namespace qsbit
