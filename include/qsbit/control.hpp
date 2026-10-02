#pragma once

#include "qsbit/time.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qsbit {
enum class ActionKind { IdealGate, Pulse, Acquire, DiscriminatorArm };
enum class ExecutionFlag { Always, LastOne, LastZero, Equal };
struct ResourceUse {
  std::uint32_t id = 0;
  bool exclusive = true;
  bool operator==(const ResourceUse &) const = default;
};
struct EventSpec {
  ActionKind kind = ActionKind::IdealGate;
  std::uint32_t port = 0;
  std::string operation = "x";
  std::vector<std::uint32_t> targets;
  std::vector<ResourceUse> resources;
  Tick delay = 0, duration = 20, discriminator_delay = 20;
  double amplitude = 0.0;
  std::string axis = "x";
  bool separate_arm = false;
  ExecutionFlag execution_flag = ExecutionFlag::Always;
  bool operator==(const EventSpec &) const = default;
};
struct Mapping {
  std::uint32_t port = 0, codeword = 0;
  std::vector<EventSpec> actions;
};
struct Profile {
  Clock cpu{5, 0}, tcu{20, 0};
  Tick start = 1000, watchdog = 1000000;
  std::uint32_t memory_latency = 1, command_latency = 1, reply_latency = 1;
  std::uint32_t cpu_result_latency = 1, fast_result_latency = 2;
  std::uint32_t timing_capacity = 32, event_capacity = 32, staging_capacity = 16;
  std::uint32_t result_capacity = 8, ports = 8, qubits = 2;
  std::uint32_t firing_width = 1, seed = 1;
  bool fast_feedback = true;
  std::vector<Mapping> mappings;
  void validate() const;
  [[nodiscard]] const Mapping &mapping(std::uint32_t port, std::uint32_t codeword) const;
  [[nodiscard]] std::string fingerprint() const;
};
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
};
struct TimingPoint {
  Epoch epoch = 0;
  Id label = 0;
  Tick interval = 0;
  std::vector<Id> manifest;
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
};

[[nodiscard]] std::vector<OperationEvent>
decode_codeword(const Profile &profile, std::uint32_t port, std::uint32_t codeword, Epoch epoch,
                Id instruction, Id first_event, std::optional<MeasurementReference> reference = {});
void validate_timing_events(const TimingEvents &events, const Profile &profile);

} // namespace qsbit
