#include "config_json.hpp"
#include <limits>
#include <set>

namespace qsbit {
namespace {
void keys(const Json &object, std::initializer_list<const char *> allowed) {
  require(object.is_object(), ErrorCode::InvalidProfile, "expected a JSON object");
  std::set<std::string> names;
  for (auto name : allowed)
    names.insert(name);
  for (const auto &[name, value] : object.items()) {
    (void)value;
    require(names.contains(name), ErrorCode::InvalidProfile, "unknown profile key: " + name);
  }
}
template <typename T> void integer(const Json &object, const char *key, T &destination) {
  if (!object.contains(key))
    return;
  const auto &value = object.at(key);
  require(value.is_number_integer() && (!value.is_number_integer() || value.is_number_unsigned() ||
                                        value.get<std::int64_t>() >= 0),
          ErrorCode::InvalidProfile, std::string("expected nonnegative integer for ") + key);
  const auto number = value.get<std::uint64_t>();
  require(number <= std::numeric_limits<T>::max(), ErrorCode::InvalidProfile,
          std::string("integer overflow for ") + key);
  destination = static_cast<T>(number);
}
std::string kind_name(ActionKind kind) {
  switch (kind) {
  case ActionKind::IdealGate:
    return "gate";
  case ActionKind::Pulse:
    return "pulse";
  case ActionKind::Acquire:
    return "acquire";
  case ActionKind::DiscriminatorArm:
    return "arm";
  }
  throw Fault(ErrorCode::InvalidProfile, "invalid action kind");
}
ActionKind kind_value(const std::string &name) {
  if (name == "gate")
    return ActionKind::IdealGate;
  if (name == "pulse")
    return ActionKind::Pulse;
  if (name == "acquire")
    return ActionKind::Acquire;
  if (name == "arm")
    return ActionKind::DiscriminatorArm;
  throw Fault(ErrorCode::InvalidProfile, "unsupported action kind: " + name);
}
} // namespace
void apply_profile(Profile &p, const Json &input) {
  keys(input, {"schema",
               "cpu",
               "tcu",
               "start",
               "watchdog",
               "memory_latency",
               "command_latency",
               "reply_latency",
               "cpu_result_latency",
               "fast_result_latency",
               "timing_capacity",
               "event_capacity",
               "staging_capacity",
               "result_slots",
               "history_depth",
               "ports",
               "qubits",
               "firing_width",
               "seed",
               "fast_feedback",
               "mappings"});
  if (input.contains("schema"))
    require(input["schema"] == 1, ErrorCode::InvalidProfile, "unsupported profile schema");
  for (auto pair : {std::pair{"cpu", &p.cpu}, std::pair{"tcu", &p.tcu}})
    if (input.contains(pair.first)) {
      const auto &clock = input.at(pair.first);
      keys(clock, {"period", "phase"});
      integer(clock, "period", pair.second->period);
      integer(clock, "phase", pair.second->phase);
    }
#define QS_FIELD(field) integer(input, #field, p.field)
  QS_FIELD(start);
  QS_FIELD(watchdog);
  QS_FIELD(memory_latency);
  QS_FIELD(command_latency);
  QS_FIELD(reply_latency);
  QS_FIELD(cpu_result_latency);
  QS_FIELD(fast_result_latency);
  QS_FIELD(timing_capacity);
  QS_FIELD(event_capacity);
  QS_FIELD(staging_capacity);
  QS_FIELD(result_slots);
  QS_FIELD(history_depth);
  QS_FIELD(ports);
  QS_FIELD(qubits);
  QS_FIELD(firing_width);
  QS_FIELD(seed);
#undef QS_FIELD
  if (input.contains("fast_feedback"))
    p.fast_feedback = input.at("fast_feedback").get<bool>();
  if (input.contains("mappings")) {
    require(input["mappings"].is_array(), ErrorCode::InvalidProfile, "mappings must be an array");
    p.mappings.clear();
    for (const auto &mapping : input["mappings"]) {
      keys(mapping, {"port", "codeword", "actions"});
      Mapping map;
      require(mapping.contains("port") && mapping.contains("codeword") &&
                  mapping.contains("actions"),
              ErrorCode::InvalidProfile, "incomplete mapping");
      integer(mapping, "port", map.port);
      integer(mapping, "codeword", map.codeword);
      require(mapping["actions"].is_array(), ErrorCode::InvalidProfile, "actions must be an array");
      for (const auto &spec : mapping["actions"]) {
        keys(spec, {"kind", "port", "operation", "targets", "resources", "delay", "duration",
                    "discriminator_delay", "amplitude", "axis", "separate_arm"});
        ActionSpec action;
        action.port = map.port;
        action.kind = kind_value(spec.value("kind", std::string("gate")));
        action.operation = spec.value("operation", std::string("x"));
        action.axis = spec.value("axis", std::string("x"));
        action.amplitude = spec.value("amplitude", 0.0);
        action.separate_arm = spec.value("separate_arm", false);
        integer(spec, "port", action.port);
        integer(spec, "delay", action.delay);
        integer(spec, "duration", action.duration);
        integer(spec, "discriminator_delay", action.discriminator_delay);
        require(spec.contains("targets") && spec["targets"].is_array(), ErrorCode::InvalidProfile,
                "action targets are required");
        for (const auto &value : spec["targets"]) {
          std::uint32_t target = 0;
          integer(Json{{"target", value}}, "target", target);
          action.targets.push_back(target);
        }
        if (spec.contains("resources")) {
          require(spec["resources"].is_array(), ErrorCode::InvalidProfile,
                  "resources must be an array");
          for (const auto &value : spec["resources"]) {
            keys(value, {"id", "exclusive"});
            require(value.contains("id"), ErrorCode::InvalidProfile, "resource ID missing");
            ResourceUse resource;
            integer(value, "id", resource.id);
            resource.exclusive = value.value("exclusive", true);
            action.resources.push_back(resource);
          }
        }
        map.actions.push_back(std::move(action));
      }
      p.mappings.push_back(std::move(map));
    }
  }
  p.validate();
}
Json profile_json(const Profile &p) {
  Json j{{"schema", 1},
         {"cpu", {{"period", p.cpu.period}, {"phase", p.cpu.phase}}},
         {"tcu", {{"period", p.tcu.period}, {"phase", p.tcu.phase}}}};
#define QS_FIELD(field) j[#field] = p.field
  QS_FIELD(start);
  QS_FIELD(watchdog);
  QS_FIELD(memory_latency);
  QS_FIELD(command_latency);
  QS_FIELD(reply_latency);
  QS_FIELD(cpu_result_latency);
  QS_FIELD(fast_result_latency);
  QS_FIELD(timing_capacity);
  QS_FIELD(event_capacity);
  QS_FIELD(staging_capacity);
  QS_FIELD(result_slots);
  QS_FIELD(history_depth);
  QS_FIELD(ports);
  QS_FIELD(qubits);
  QS_FIELD(firing_width);
  QS_FIELD(seed);
  QS_FIELD(fast_feedback);
#undef QS_FIELD
  j["mappings"] = Json::array();
  for (const auto &mapping : p.mappings) {
    Json map{{"port", mapping.port}, {"codeword", mapping.codeword}, {"actions", Json::array()}};
    for (const auto &action : mapping.actions) {
      Json spec{{"kind", kind_name(action.kind)},
                {"port", action.port},
                {"operation", action.operation},
                {"targets", action.targets},
                {"delay", action.delay},
                {"duration", action.duration},
                {"discriminator_delay", action.discriminator_delay},
                {"amplitude", action.amplitude},
                {"axis", action.axis},
                {"separate_arm", action.separate_arm},
                {"resources", Json::array()}};
      for (const auto &r : action.resources)
        spec["resources"].push_back({{"id", r.id}, {"exclusive", r.exclusive}});
      map["actions"].push_back(std::move(spec));
    }
    j["mappings"].push_back(std::move(map));
  }
  return j;
}
} // namespace qsbit
