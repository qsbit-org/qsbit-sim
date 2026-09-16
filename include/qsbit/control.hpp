#pragma once

#include "qsbit/time.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qsbit {
enum class ActionKind { IdealGate, Pulse, Acquire, DiscriminatorArm };
struct ResourceUse {
  std::uint32_t id = 0;
  bool exclusive = true;
  bool operator==(const ResourceUse &) const = default;
};
struct ActionSpec {
  ActionKind kind = ActionKind::IdealGate;
  std::uint32_t port = 0;
  std::string operation = "x";
  std::vector<std::uint32_t> targets;
  std::vector<ResourceUse> resources;
  Tick delay = 0, duration = 20, discriminator_delay = 20;
  double amplitude = 0.0;
  std::string axis = "x";
  bool separate_arm = false;
  bool operator==(const ActionSpec &) const = default;
};
struct Mapping {
  std::uint32_t port = 0, codeword = 0;
  std::vector<ActionSpec> actions;
};
struct Profile {
  Clock cpu{5, 0}, tcu{20, 0};
  Tick start = 1000, watchdog = 1000000;
  std::uint32_t memory_latency = 1, command_latency = 1, reply_latency = 1;
  std::uint32_t cpu_result_latency = 1, fast_result_latency = 2;
  std::uint32_t timing_capacity = 32, event_capacity = 32, staging_capacity = 16;
  std::uint32_t result_slots = 8, history_depth = 8, ports = 8, qubits = 2;
  std::uint32_t firing_width = 1, seed = 1;
  bool fast_feedback = true;
  std::vector<Mapping> mappings;
  void validate() const;
  [[nodiscard]] const Mapping &mapping(std::uint32_t port, std::uint32_t codeword) const;
  [[nodiscard]] std::string fingerprint() const;
};
struct Token {
  Epoch epoch = 0;
  Id measurement = 0;
  std::uint32_t slot = 0, generation = 0, handle = 0, target = 0;
  bool operator==(const Token &) const = default;
};
struct Condition {
  Token token;
  bool expected = false;
};
struct ReservedEvent {
  Epoch epoch = 0;
  Id id = 0, instruction = 0, label = 0;
  std::uint32_t source_port = 0, codeword = 0;
  ActionSpec action;
  std::optional<Token> token;
  std::optional<Condition> condition;
};
struct TimingPoint {
  Epoch epoch = 0;
  Id label = 0;
  Tick interval = 0;
  std::vector<Id> manifest;
};
struct Group {
  TimingPoint point;
  std::vector<ReservedEvent> events;
  std::string configuration;
};
struct GroupReply {
  Id label = 0;
};
struct EndOfStream {
  Id last_label = 0;
};
struct Completion {
  Token token;
  bool value = false;
};
struct LaunchBatch {
  Epoch epoch = 0;
  Id label = 0;
  Tick fire_tick = 0;
  std::vector<ReservedEvent> events;
};
struct PhysicalAction {
  ReservedEvent event;
  Tick start = 0, end = 0;
};

[[nodiscard]] std::vector<ReservedEvent> lower(const Profile &profile, std::uint32_t port,
                                               std::uint32_t codeword, Epoch epoch, Id instruction,
                                               Id first_event, std::optional<Token> token = {},
                                               std::optional<Condition> condition = {});
void validate_group(const Group &group, const Profile &profile);
} // namespace qsbit
