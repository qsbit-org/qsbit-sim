#include "qsbit/timing_config.hpp"
#include "qsbit/error.hpp"
#include "qsbit/profile.hpp"
#include <cstdint>

namespace qsbit {
TransportConfig transport_config(const Profile &p) {
  return {p.cpu,
          p.tcu,
          p.command_latency,
          p.reply_latency,
          p.cpu_result_latency,
          p.fast_result_latency,
          p.result_capacity};
}
TimingConfig timing_config(const Profile &p) {
  TimingConfig result{p.event_capacity, p.staging_capacity, p.ports,         p.qubits,
                      p.firing_width,   p.fast_feedback,    p.fingerprint(), {}};
  for (const auto &mapping : p.mappings)
    require(result.mappings.emplace(std::pair{mapping.port, mapping.codeword}, mapping).second,
            ErrorCode::InvalidProfile, "duplicate port/codeword mapping");
  return result;
}
const Mapping &TimingConfig::mapping(std::uint32_t port, std::uint32_t codeword) const {
  require(port < ports, ErrorCode::InvalidPort, "port is outside configured map");
  const auto it = mappings.find({port, codeword});
  require(it != mappings.end(), ErrorCode::InvalidCodeword, "codeword is unmapped on this port");
  return it->second;
}
TcuConfig tcu_config(const Profile &p) {
  return {timing_config(p), p.tcu, p.start, p.timing_capacity};
}
} // namespace qsbit
