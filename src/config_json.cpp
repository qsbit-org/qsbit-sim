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
std::string flag_name(ExecutionFlag flag) {
  switch (flag) {
  case ExecutionFlag::Always:
    return "always";
  case ExecutionFlag::LastOne:
    return "last_one";
  case ExecutionFlag::LastZero:
    return "last_zero";
  case ExecutionFlag::Equal:
    return "equal";
  }
  throw Fault(ErrorCode::InvalidProfile, "invalid execution flag");
}
ExecutionFlag flag_value(const std::string &name) {
  for (auto flag : {ExecutionFlag::Always, ExecutionFlag::LastOne, ExecutionFlag::LastZero,
                    ExecutionFlag::Equal})
    if (flag_name(flag) == name)
      return flag;
  throw Fault(ErrorCode::InvalidProfile, "unsupported execution flag: " + name);
}
} // namespace
EqasmConfiguration eqasm_configuration(const Json &input) {
  keys(input, {"qubit_pairs", "microcode"});
  EqasmConfiguration configuration;
  if (input.contains("qubit_pairs")) {
    require(input["qubit_pairs"].is_array() && input["qubit_pairs"].size() <= 16,
            ErrorCode::InvalidProfile, "eQASM qubit_pairs must contain at most sixteen pairs");
    for (const auto &pair : input["qubit_pairs"]) {
      require(pair.is_array() && pair.size() == 2, ErrorCode::InvalidProfile,
              "eQASM qubit pair must contain two qubits");
      std::array<std::uint32_t, 2> parsed{};
      for (std::size_t i = 0; i < 2; ++i)
        integer(Json{{"qubit", pair[i]}}, "qubit", parsed[i]);
      configuration.qubit_pairs.push_back(parsed);
    }
  }
  require(input.contains("microcode") && input["microcode"].is_array() &&
              input["microcode"].size() <= 511 * 16,
          ErrorCode::InvalidProfile, "eQASM requires a microcode array");
  for (const auto &entry : input["microcode"]) {
    keys(entry, {"opcode", "targets", "port", "codeword"});
    require(entry.contains("opcode") && entry.contains("targets") && entry.contains("port") &&
                entry.contains("codeword") && entry["targets"].is_array() &&
                (entry["targets"].size() == 1 || entry["targets"].size() == 2),
            ErrorCode::InvalidProfile,
            "eQASM microcode requires opcode, targets, port and codeword");
    EqasmMicrocode result;
    integer(entry, "opcode", result.opcode);
    integer(entry, "port", result.port);
    integer(entry, "codeword", result.codeword);
    for (const auto &target : entry["targets"]) {
      std::uint32_t qubit = 0;
      integer(Json{{"qubit", target}}, "qubit", qubit);
      result.targets.push_back(qubit);
    }
    configuration.microcode.push_back(std::move(result));
  }
  return configuration;
}

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
               "result_capacity",
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
  QS_FIELD(result_capacity);
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
                    "discriminator_delay", "amplitude", "axis", "separate_arm", "execution_flag"});
        EventSpec action;
        action.port = map.port;
        action.kind = kind_value(spec.value("kind", std::string("gate")));
        action.operation = spec.value("operation", std::string("x"));
        action.axis = spec.value("axis", std::string("x"));
        action.amplitude = spec.value("amplitude", 0.0);
        action.separate_arm = spec.value("separate_arm", false);
        action.execution_flag = flag_value(spec.value("execution_flag", std::string("always")));
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
  QS_FIELD(result_capacity);
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
                {"execution_flag", flag_name(action.execution_flag)},
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
