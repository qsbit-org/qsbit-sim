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
  case ActionKind::GateOutput:
    return "gate_output";
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
  if (name == "gate_output")
    return ActionKind::GateOutput;
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
               "mappings",
               "two_qubit_gates"});
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
  if (input.contains("two_qubit_gates")) {
    require(input["two_qubit_gates"].is_array(), ErrorCode::InvalidProfile,
            "two_qubit_gates must be an array");
    p.two_qubit_gates.clear();
    for (const auto &spec : input["two_qubit_gates"]) {
      keys(spec, {"name", "operation", "targets", "inputs", "resources", "duration"});
      require(spec.contains("name") && spec.contains("operation") && spec.contains("targets") &&
                  spec["targets"].is_array() && spec.contains("inputs") &&
                  spec["inputs"].is_array() && spec.contains("duration"),
              ErrorCode::InvalidProfile, "incomplete two-qubit gate definition");
      TwoQubitGate gate;
      gate.name = spec["name"].get<std::string>();
      gate.operation = spec["operation"].get<std::string>();
      integer(spec, "duration", gate.duration);
      for (const auto &value : spec["targets"]) {
        std::uint32_t target = 0;
        integer(Json{{"target", value}}, "target", target);
        gate.targets.push_back(target);
      }
      for (const auto &value : spec["inputs"]) {
        keys(value, {"core", "port", "codeword"});
        require(value.contains("core") && value.contains("port") && value.contains("codeword"),
                ErrorCode::InvalidProfile, "incomplete gate input");
        GateInput endpoint;
        integer(value, "core", endpoint.core);
        integer(value, "port", endpoint.port);
        integer(value, "codeword", endpoint.codeword);
        gate.inputs.push_back(endpoint);
      }
      if (spec.contains("resources")) {
        require(spec["resources"].is_array(), ErrorCode::InvalidProfile,
                "gate resources must be an array");
        for (const auto &value : spec["resources"]) {
          keys(value, {"id", "exclusive"});
          require(value.contains("id"), ErrorCode::InvalidProfile, "resource ID missing");
          ResourceUse resource;
          integer(value, "id", resource.id);
          resource.exclusive = value.value("exclusive", true);
          gate.resources.push_back(resource);
        }
      }
      p.two_qubit_gates.push_back(std::move(gate));
    }
  }
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
        require(spec.is_object(), ErrorCode::InvalidProfile, "action must be an object");
        EventSpec action;
        action.port = map.port;
        const auto kind = kind_value(spec.value("kind", std::string("gate")));
        switch (kind) {
        case ActionKind::IdealGate:
          keys(spec, {"kind", "port", "delay", "duration", "operation", "targets", "resources",
                      "amplitude", "execution_flag"});
          action.spec = GateSpec{};
          break;
        case ActionKind::Pulse:
          keys(spec, {"kind", "port", "delay", "duration", "operation", "targets", "resources",
                      "amplitude", "axis", "execution_flag"});
          action.spec = PulseSpec{};
          break;
        case ActionKind::Acquire:
          keys(spec, {"kind", "port", "delay", "duration", "operation", "targets", "resources",
                      "discriminator_delay", "separate_arm"});
          action.spec = AcquireSpec{};
          break;
        case ActionKind::DiscriminatorArm:
          keys(spec, {"kind", "port", "delay", "duration", "operation", "targets", "resources"});
          action.spec = ArmSpec{};
          break;
        case ActionKind::GateOutput:
          keys(spec, {"kind", "port", "delay", "duration", "gate"});
          action.spec = GateOutputSpec{spec.value("gate", std::string{})};
          action.duration = p.gate(action.get<GateOutputSpec>().gate).duration;
          break;
        }
        integer(spec, "port", action.port);
        integer(spec, "delay", action.delay);
        integer(spec, "duration", action.duration);
        std::visit(
            [&](auto &value) {
              if constexpr (requires { value.operands.targets; }) {
                value.operation = spec.value("operation", std::string("x"));
                require(spec.contains("targets") && spec["targets"].is_array(),
                        ErrorCode::InvalidProfile, "action targets are required");
                for (const auto &entry : spec["targets"]) {
                  std::uint32_t target = 0;
                  integer(Json{{"target", entry}}, "target", target);
                  value.operands.targets.push_back(target);
                }
                if (spec.contains("resources")) {
                  require(spec["resources"].is_array(), ErrorCode::InvalidProfile,
                          "resources must be an array");
                  for (const auto &entry : spec["resources"]) {
                    keys(entry, {"id", "exclusive"});
                    require(entry.contains("id"), ErrorCode::InvalidProfile, "resource ID missing");
                    ResourceUse resource;
                    integer(entry, "id", resource.id);
                    resource.exclusive = entry.value("exclusive", true);
                    value.operands.resources.push_back(resource);
                  }
                }
              }
              if constexpr (requires { value.amplitude; })
                value.amplitude = spec.value("amplitude", 0.0);
              if constexpr (requires { value.axis; })
                value.axis = spec.value("axis", std::string("x"));
              if constexpr (requires { value.execution_flag; })
                value.execution_flag =
                    flag_value(spec.value("execution_flag", std::string("always")));
              if constexpr (requires { value.discriminator_delay; }) {
                integer(spec, "discriminator_delay", value.discriminator_delay);
                value.separate_arm = spec.value("separate_arm", false);
              }
            },
            action.spec);
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
  j["two_qubit_gates"] = Json::array();
  for (const auto &gate : p.two_qubit_gates) {
    Json spec{{"name", gate.name},       {"operation", gate.operation},
              {"targets", gate.targets}, {"duration", gate.duration},
              {"inputs", Json::array()}, {"resources", Json::array()}};
    for (const auto &input : gate.inputs)
      spec["inputs"].push_back(
          {{"core", input.core}, {"port", input.port}, {"codeword", input.codeword}});
    for (const auto &r : gate.resources)
      spec["resources"].push_back({{"id", r.id}, {"exclusive", r.exclusive}});
    j["two_qubit_gates"].push_back(std::move(spec));
  }
  for (const auto &mapping : p.mappings) {
    Json map{{"port", mapping.port}, {"codeword", mapping.codeword}, {"actions", Json::array()}};
    for (const auto &action : mapping.actions) {
      Json spec{{"kind", kind_name(action.kind())},
                {"port", action.port},
                {"delay", action.delay},
                {"duration", action.duration}};
      std::visit(
          [&](const auto &value) {
            if constexpr (requires { value.operands.targets; }) {
              spec["operation"] = value.operation;
              spec["targets"] = value.operands.targets;
              spec["resources"] = Json::array();
              for (const auto &resource : value.operands.resources)
                spec["resources"].push_back(
                    {{"id", resource.id}, {"exclusive", resource.exclusive}});
            }
            if constexpr (requires { value.amplitude; })
              spec["amplitude"] = value.amplitude;
            if constexpr (requires { value.axis; })
              spec["axis"] = value.axis;
            if constexpr (requires { value.execution_flag; })
              spec["execution_flag"] = flag_name(value.execution_flag);
            if constexpr (requires { value.discriminator_delay; }) {
              spec["discriminator_delay"] = value.discriminator_delay;
              spec["separate_arm"] = value.separate_arm;
            }
            if constexpr (requires { value.gate; })
              spec["gate"] = value.gate;
          },
          action.spec);
      map["actions"].push_back(std::move(spec));
    }
    j["mappings"].push_back(std::move(map));
  }
  return j;
}
} // namespace qsbit
