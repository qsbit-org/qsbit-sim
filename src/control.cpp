#include "qsbit/control.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace qsbit {
void Profile::validate() const {
  cpu.validate();
  tcu.validate();
  require(tcu.edge(start) && watchdog > start, ErrorCode::InvalidProfile,
          "invalid start or watchdog");
  for (const auto n : {memory_latency, command_latency, reply_latency, cpu_result_latency,
                       fast_result_latency, timing_capacity, event_capacity, staging_capacity,
                       result_slots, history_depth, ports, qubits, firing_width})
    require(n > 0, ErrorCode::InvalidProfile, "capacities and latencies must be positive");
  require(result_slots <= 65535 && ports <= 65535 && qubits <= 65535, ErrorCode::InvalidProfile,
          "profile exceeds v1 index bounds");
  std::set<std::pair<std::uint32_t, std::uint32_t>> keys;
  for (const auto &map : mappings) {
    require(map.port < ports && keys.insert({map.port, map.codeword}).second,
            ErrorCode::InvalidProfile, "duplicate or invalid port mapping");
    require(!map.actions.empty(), ErrorCode::InvalidProfile, "empty action mapping");
    unsigned acquisitions = 0, arms = 0;
    bool separate = false;
    for (const auto &a : map.actions) {
      require(a.port < ports && !a.targets.empty() && a.duration > 0 && std::isfinite(a.amplitude),
              ErrorCode::InvalidProfile, "invalid action descriptor");
      std::set<std::uint32_t> targets;
      for (auto q : a.targets)
        require(q < qubits && targets.insert(q).second, ErrorCode::InvalidProfile,
                "invalid target list");
      std::set<std::uint32_t> resources;
      for (const auto &r : a.resources)
        require(resources.insert(r.id).second, ErrorCode::InvalidProfile,
                "duplicate action resource");
      if (a.kind == ActionKind::Acquire) {
        ++acquisitions;
        separate = a.separate_arm;
        require(a.targets.size() == 1, ErrorCode::InvalidProfile,
                "one token measures one target in v1");
      }
      if (a.kind == ActionKind::DiscriminatorArm)
        ++arms;
      if (a.kind == ActionKind::Pulse)
        require(a.targets.size() == 1 && (a.axis == "x" || a.axis == "y" || a.axis == "z"),
                ErrorCode::InvalidProfile, "unsupported pulse descriptor");
    }
    require(acquisitions <= 1 && arms == (separate ? 1U : 0U), ErrorCode::InvalidProfile,
            "acquisition and discriminator arm must be paired");
    if (separate) {
      const auto acquisition =
          std::find_if(map.actions.begin(), map.actions.end(),
                       [](const ActionSpec &a) { return a.kind == ActionKind::Acquire; });
      const auto arm =
          std::find_if(map.actions.begin(), map.actions.end(),
                       [](const ActionSpec &a) { return a.kind == ActionKind::DiscriminatorArm; });
      require(acquisition->targets == arm->targets, ErrorCode::InvalidProfile,
              "acquisition and discriminator arm must address the same target");
    }
  }
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
                       result_slots, history_depth, ports, qubits, firing_width, seed})
    text << ' ' << n;
  text << ' ' << fast_feedback << std::setprecision(17);
  for (const auto &m : mappings) {
    text << " m " << m.port << ' ' << m.codeword;
    for (const auto &a : m.actions) {
      text << " a " << static_cast<int>(a.kind) << ' ' << a.port << ' ' << std::quoted(a.operation)
           << ' ' << a.delay << ' ' << a.duration << ' ' << a.discriminator_delay << ' '
           << a.amplitude << ' ' << std::quoted(a.axis) << ' ' << a.separate_arm;
      for (auto q : a.targets)
        text << " q " << q;
      for (auto r : a.resources)
        text << " r " << r.id << ' ' << r.exclusive;
    }
  }
  // Stable FNV-1a identifier, not a cryptographic integrity claim.
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char c : text.str()) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}
std::vector<ReservedEvent> lower(const Profile &profile, std::uint32_t port, std::uint32_t codeword,
                                 Epoch epoch, Id instruction, Id first_event,
                                 std::optional<Token> token, std::optional<Condition> condition) {
  const auto &map = profile.mapping(port, codeword);
  std::vector<ReservedEvent> events;
  for (const auto &action : map.actions) {
    const bool readout =
        action.kind == ActionKind::Acquire || action.kind == ActionKind::DiscriminatorArm;
    require(!readout || token.has_value(), ErrorCode::InvalidToken,
            "readout has no reserved token");
    require(!(readout && condition), ErrorCode::UnsupportedCapability,
            "conditional measurement is unsupported");
    require(!condition || profile.fast_feedback, ErrorCode::UnsupportedCapability,
            "fast feedback is disabled");
    ReservedEvent event{
        epoch,  checked_add(first_event, events.size()), instruction, 0, port, codeword,
        action, readout ? token : std::nullopt,          condition};
    events.push_back(std::move(event));
  }
  return events;
}
void validate_group(const Group &group, const Profile &profile) {
  require(group.configuration == profile.fingerprint(), ErrorCode::Protocol,
          "group profile mismatch");
  require(group.events.size() == group.point.manifest.size(), ErrorCode::ManifestMismatch,
          "manifest count differs from event count");
  require(group.events.size() <= profile.staging_capacity, ErrorCode::Capacity,
          "oversized staged group");
  std::set<Id> ids;
  std::vector<std::uint32_t> counts(profile.ports, 0);
  for (std::size_t i = 0; i < group.events.size(); ++i) {
    const auto &e = group.events[i];
    require(e.epoch == group.point.epoch && e.label == group.point.label && e.id != 0 &&
                e.id == group.point.manifest[i] && ids.insert(e.id).second,
            ErrorCode::ManifestMismatch, "invalid manifested event identity");
    require(e.action.port < profile.ports, ErrorCode::InvalidPort,
            "resolved output port is invalid");
    ++counts[e.action.port];
    require(counts[e.action.port] <= profile.firing_width &&
                counts[e.action.port] <= profile.event_capacity,
            ErrorCode::Capacity, "group exceeds per-port firing or storage capacity");
    const auto &map = profile.mapping(e.source_port, e.codeword);
    require(std::find(map.actions.begin(), map.actions.end(), e.action) != map.actions.end(),
            ErrorCode::Protocol, "resolved descriptor differs from immutable mapping");
  }
}
} // namespace qsbit
