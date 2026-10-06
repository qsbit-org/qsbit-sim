#pragma once

#include "qsbit/memory.hpp"
#include "qsbit/trace.hpp"
#include <deque>
#include <functional>
#include <map>
#include <span>

namespace qsbit {
struct DecoderConfig {
  std::uint32_t id = 0, measurements = 0, outputs = 0;
  Tick latency = 1, initiation_interval = 1;
  std::function<std::vector<bool>(std::span<const std::uint8_t>)> decode;
};
struct DecoderSystemConfig {
  std::uint32_t base = 0x40000000, request_capacity = 16, result_capacity = 16;
  Tick link_latency = 1;
  std::uint32_t bytes_per_tick = 1, packet_overhead = 16;
  std::vector<DecoderConfig> decoders;
};
class DecoderSystem {
public:
  DecoderSystem(DecoderSystemConfig config, Trace &trace);
  [[nodiscard]] bool contains(std::uint32_t address) const;
  void validate_memory(const ProgramImage &image) const;
  std::optional<std::uint32_t> access(std::uint32_t core, const MemoryRequest &request, Tick now,
                                      Epoch epoch);
  void step(Tick now, Epoch epoch);
  void reset();
  [[nodiscard]] bool idle() const;
  [[nodiscard]] std::optional<Tick> next_boundary(Tick now) const;

private:
  struct Registers {
    std::uint32_t decoder = 0, count = 0, low = 0, high = 0, tag = 0;
  };
  struct Session {
    std::vector<std::uint8_t> measurements;
    std::optional<std::uint64_t> result;
    bool submitted = false;
  };
  struct Request {
    Id id;
    std::uint32_t core, decoder, count, tag;
    std::uint64_t data;
    Tick sent, arrival;
    bool transmitted = false;
  };
  struct Job {
    Id id;
    std::uint32_t core, decoder;
    Tick start, completion;
    std::uint64_t result;
    std::optional<Tick> arrival;
    bool started = false, completed = false;
  };
  const DecoderConfig &decoder(std::uint32_t id) const;
  void event(Tick now, Epoch epoch, const char *kind, Id id, std::uint32_t core,
             std::uint32_t decoder, std::uint32_t tag = 0);
  DecoderSystemConfig config_;
  Trace &trace_;
  std::map<std::uint32_t, Registers> registers_;
  std::map<std::pair<std::uint32_t, std::uint32_t>, Session> sessions_;
  std::map<std::uint32_t, Tick> available_;
  std::deque<Request> requests_;
  std::optional<Request> reset_request_;
  std::deque<Job> jobs_;
  Tick tx_available_ = 0, rx_available_ = 0;
  Id next_ = 1;
};
} // namespace qsbit
