#include "qsbit/control.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>

namespace qsbit {
const std::vector<std::uint32_t> &EventSpec::targets() const {
  static const std::vector<std::uint32_t> empty;
  return std::visit(
      [](const auto &value) -> const std::vector<std::uint32_t> & {
        if constexpr (requires { value.targets; })
          return value.targets;
        else
          return empty;
      },
      spec);
}
const std::vector<ResourceUse> &EventSpec::resources() const {
  static const std::vector<ResourceUse> empty;
  return std::visit(
      [](const auto &value) -> const std::vector<ResourceUse> & {
        if constexpr (requires { value.resources; })
          return value.resources;
        else
          return empty;
      },
      spec);
}
std::string EventSpec::operation() const {
  return std::visit(
      [](const auto &value) -> std::string {
        if constexpr (requires { value.operation; })
          return value.operation;
        else
          return {};
      },
      spec);
}
ExecutionFlag EventSpec::execution_flag() const {
  return std::visit(
      [](const auto &value) {
        if constexpr (requires { value.execution_flag; })
          return value.execution_flag;
        else
          return ExecutionFlag::Always;
      },
      spec);
}
void Profile::validate() const {
  cpu.validate();
  tcu.validate();
  require(tcu.edge(start) && watchdog > start, ErrorCode::InvalidProfile,
          "invalid start or watchdog");
  for (const auto n : {memory_latency, command_latency, reply_latency, cpu_result_latency,
                       fast_result_latency, timing_capacity, event_capacity, staging_capacity,
                       result_capacity, ports, qubits, firing_width})
    require(n > 0, ErrorCode::InvalidProfile, "capacities and latencies must be positive");
  require(result_capacity <= 65535 && ports <= 65535 && qubits <= 32, ErrorCode::InvalidProfile,
          "profile exceeds index bounds");
  std::set<std::string> gate_names;
  std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> gate_inputs;
  for (const auto &g : two_qubit_gates) {
    require(!g.name.empty() && gate_names.insert(g.name).second && !g.operation.empty() &&
                g.duration > 0 && g.targets.size() == 2 && g.targets[0] != g.targets[1] &&
                g.targets[0] < qubits && g.targets[1] < qubits && g.inputs.size() == 2,
            ErrorCode::InvalidProfile, "invalid two-qubit gate definition");
    require(g.inputs[0].core != g.inputs[1].core || g.inputs[0].port != g.inputs[1].port,
            ErrorCode::InvalidProfile, "two-qubit gate requires two distinct output ports");
    for (const auto &input : g.inputs)
      require(input.core <= 131071 &&
                  gate_inputs.emplace(input.core, input.port, input.codeword).second,
              ErrorCode::InvalidProfile, "invalid or ambiguous two-qubit gate input");
    std::set<std::uint32_t> resources;
    for (const auto &r : g.resources)
      require(resources.insert(r.id).second, ErrorCode::InvalidProfile, "duplicate gate resource");
  }
  std::set<std::pair<std::uint32_t, std::uint32_t>> keys;
  for (const auto &map : mappings) {
    require(map.port < ports && keys.insert({map.port, map.codeword}).second,
            ErrorCode::InvalidProfile, "duplicate or invalid port mapping");
    require(!map.actions.empty(), ErrorCode::InvalidProfile, "empty action mapping");
    unsigned acquisitions = 0, arms = 0;
    bool separate = false;
    for (const auto &a : map.actions) {
      require(a.port < ports && (a.kind() == ActionKind::GateOutput || !a.targets().empty()) &&
                  a.duration > 0,
              ErrorCode::InvalidProfile, "invalid action descriptor");
      if (a.kind() == ActionKind::GateOutput) {
        require(map.actions.size() == 1 && a.port == map.port, ErrorCode::InvalidProfile,
                "gate output must be one port action");
        require(a.duration == gate(a.get<GateOutputSpec>().gate).duration,
                ErrorCode::InvalidProfile, "gate output duration differs from gate duration");
      }
      require(a.execution_flag() >= ExecutionFlag::Always &&
                  a.execution_flag() <= ExecutionFlag::Equal,
              ErrorCode::InvalidProfile, "invalid execution flag");
      require(a.execution_flag() == ExecutionFlag::Always || a.targets().size() == 1,
              ErrorCode::InvalidProfile, "execution flags require a single-qubit gate or pulse");
      std::set<std::uint32_t> targets;
      for (auto q : a.targets())
        require(q < qubits && targets.insert(q).second, ErrorCode::InvalidProfile,
                "invalid target list");
      std::set<std::uint32_t> resources;
      for (const auto &r : a.resources())
        require(resources.insert(r.id).second, ErrorCode::InvalidProfile,
                "duplicate action resource");
      if (a.kind() == ActionKind::Acquire) {
        ++acquisitions;
        separate = a.get<AcquireSpec>().separate_arm;
        require(a.targets().size() == 1, ErrorCode::InvalidProfile,
                "acquisition requires one target");
      }
      if (a.kind() == ActionKind::DiscriminatorArm)
        ++arms;
      std::visit(
          [](const auto &value) {
            if constexpr (requires { value.amplitude; })
              require(std::isfinite(value.amplitude), ErrorCode::InvalidProfile,
                      "nonfinite amplitude");
          },
          a.spec);
      if (a.kind() == ActionKind::Pulse)
        require(a.targets().size() == 1 &&
                    (a.get<PulseSpec>().axis == "x" || a.get<PulseSpec>().axis == "y" ||
                     a.get<PulseSpec>().axis == "z"),
                ErrorCode::InvalidProfile, "unsupported pulse descriptor");
    }
    require(acquisitions <= 1 && arms == (separate ? 1U : 0U), ErrorCode::InvalidProfile,
            "acquisition and discriminator arm must be paired");
    if (separate) {
      const auto acquisition =
          std::find_if(map.actions.begin(), map.actions.end(),
                       [](const EventSpec &a) { return a.kind() == ActionKind::Acquire; });
      const auto arm = std::find_if(map.actions.begin(), map.actions.end(), [](const EventSpec &a) {
        return a.kind() == ActionKind::DiscriminatorArm;
      });
      require(acquisition->targets() == arm->targets(), ErrorCode::InvalidProfile,
              "acquisition and discriminator arm must address the same target");
    }
  }
}
const TwoQubitGate &Profile::gate(const std::string &gate_name) const {
  const auto it = std::find_if(two_qubit_gates.begin(), two_qubit_gates.end(),
                               [&](const auto &g) { return g.name == gate_name; });
  require(it != two_qubit_gates.end(), ErrorCode::InvalidProfile,
          "unknown two-qubit gate: " + gate_name);
  return *it;
}
const Mapping &Profile::mapping(std::uint32_t port, std::uint32_t codeword) const {
  require(port < ports, ErrorCode::InvalidPort, "port is outside configured map");
  const auto it = std::find_if(mappings.begin(), mappings.end(), [&](const Mapping &m) {
    return m.port == port && m.codeword == codeword;
  });
  require(it != mappings.end(), ErrorCode::InvalidCodeword, "codeword is unmapped on this port");
  return *it;
}
std::string Profile::fingerprint() const {
  std::ostringstream text;
  text << "qsbit-profile-v1 " << cpu.period << ' ' << cpu.phase << ' ' << tcu.period << ' '
       << tcu.phase << ' ' << start << ' ' << watchdog;
  for (const auto n : {memory_latency, command_latency, reply_latency, cpu_result_latency,
                       fast_result_latency, timing_capacity, event_capacity, staging_capacity,
                       result_capacity, ports, qubits, firing_width, seed})
    text << ' ' << n;
  text << ' ' << fast_feedback << std::setprecision(17);
  for (const auto &m : mappings) {
    text << " m " << m.port << ' ' << m.codeword;
    for (const auto &a : m.actions) {
      text << " a " << static_cast<int>(a.kind()) << ' ' << a.port << ' '
           << std::quoted(a.operation()) << ' ' << a.delay << ' ' << a.duration;
      std::visit(
          [&](const auto &value) {
            if constexpr (requires { value.amplitude; })
              text << ' ' << value.amplitude;
            if constexpr (requires { value.axis; })
              text << ' ' << std::quoted(value.axis);
            if constexpr (requires { value.execution_flag; })
              text << ' ' << static_cast<int>(value.execution_flag);
            if constexpr (requires { value.discriminator_delay; })
              text << ' ' << value.discriminator_delay << ' ' << value.separate_arm;
            if constexpr (requires { value.gate; })
              text << ' ' << std::quoted(value.gate);
          },
          a.spec);
      for (auto q : a.targets())
        text << " q " << q;
      for (auto r : a.resources())
        text << " r " << r.id << ' ' << r.exclusive;
    }
  }
  for (const auto &g : two_qubit_gates) {
    text << " g " << std::quoted(g.name) << ' ' << std::quoted(g.operation) << ' ' << g.duration;
    for (auto q : g.targets)
      text << " q " << q;
    for (const auto &input : g.inputs)
      text << " i " << input.core << ' ' << input.port << ' ' << input.codeword;
    for (auto r : g.resources)
      text << " r " << r.id << ' ' << r.exclusive;
  }
  // FNV-1a configuration fingerprint.
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char c : text.str()) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}
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
