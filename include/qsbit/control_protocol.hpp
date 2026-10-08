#pragma once

#include "qsbit/profile.hpp"

namespace qsbit {
enum class UnderflowPolicy { Inherit, Strict, PauseWhenEmpty };
struct MeasurementReference {
  Epoch epoch = 0;
  Id measurement = 0;
  std::uint32_t target = 0;
  bool operator==(const MeasurementReference &) const = default;
};
struct OperationEvent {
  Epoch epoch = 0;
  Id id = 0, instruction = 0, label = 0;
  std::uint32_t source_port = 0, codeword = 0;
  EventSpec action;
  std::optional<MeasurementReference> reference;
  std::optional<std::uint32_t> core = {};
};
struct TimingPoint {
  Epoch epoch = 0;
  Id label = 0;
  Tick interval = 0;
  std::vector<Id> manifest;
  std::vector<std::uint32_t> synchronizations = {};
  UnderflowPolicy underflow_policy = UnderflowPolicy::Inherit;
};
// One enqueue request: a timing point and its associated operation events.
struct TimingEvents {
  TimingPoint point;
  std::vector<OperationEvent> events;
  std::string configuration;
};
struct EnqueueReply {
  Id label = 0;
};
struct EndOfStream {
  Id last_label = 0;
};
struct Completion {
  MeasurementReference reference;
  bool value = false;
};
struct TriggeredEvents {
  Epoch epoch = 0;
  Id label = 0;
  Tick fire_tick = 0;
  std::vector<OperationEvent> events;
};
struct ScheduledEvent {
  OperationEvent event;
  Tick start = 0, end = 0;
  EventSpec resolved;
};

[[nodiscard]] std::vector<OperationEvent>
decode_codeword(const Profile &profile, std::uint32_t port, std::uint32_t codeword, Epoch epoch,
                Id instruction, Id first_event, std::optional<MeasurementReference> reference = {});
void validate_timing_events(const TimingEvents &events, const Profile &profile);

} // namespace qsbit
