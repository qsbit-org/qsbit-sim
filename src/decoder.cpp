#include "qsbit/decoder.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace qsbit {
DecoderSystem::DecoderSystem(DecoderSystemConfig config, Trace &trace)
    : config_(std::move(config)), trace_(trace) {
  require(config_.base % 4 == 0 && config_.base <= UINT32_MAX - 31 &&
              config_.request_capacity > 0 && config_.result_capacity > 0 &&
              config_.bytes_per_tick > 0 && config_.link_latency > 0,
          ErrorCode::InvalidProfile, "invalid decoder transport configuration");
  std::set<std::uint32_t> ids;
  for (const auto &d : config_.decoders)
    require(ids.insert(d.id).second && d.measurements > 0 && d.measurements <= 1048576 &&
                d.outputs > 0 && d.outputs <= 64 && d.latency > 0 && d.initiation_interval > 0 &&
                bool(d.decode),
            ErrorCode::InvalidProfile, "invalid decoder configuration");
}
bool DecoderSystem::contains(std::uint32_t address) const {
  return !config_.decoders.empty() && address >= config_.base &&
         std::uint64_t(address) < std::uint64_t(config_.base) + 32;
}
void DecoderSystem::validate_memory(const ProgramImage &image) const {
  require(config_.decoders.empty() ||
              std::uint64_t(image.base()) + image.bytes().size() <= config_.base ||
              std::uint64_t(config_.base) + 32 <= image.base(),
          ErrorCode::InvalidProfile, "decoder MMIO overlaps program RAM");
}
const DecoderConfig &DecoderSystem::decoder(std::uint32_t id) const {
  const auto it = std::find_if(config_.decoders.begin(), config_.decoders.end(),
                               [id](const auto &d) { return d.id == id; });
  require(it != config_.decoders.end(), ErrorCode::InvalidOperand, "unknown decoder id");
  return *it;
}
void DecoderSystem::event(Tick now, Epoch epoch, const char *kind, Id id, std::uint32_t core,
                          std::uint32_t target, std::uint32_t tag) {
  TraceEvent e{now, epoch, kind, id};
  e.core = core;
  e.value = target;
  e.detail = "tag=" + std::to_string(tag) + ",requests=" + std::to_string(requests_.size()) +
             ",jobs=" + std::to_string(jobs_.size());
  trace_.emit(std::move(e));
}
std::optional<std::uint32_t> DecoderSystem::access(std::uint32_t core, const MemoryRequest &request,
                                                   Tick now, Epoch epoch) {
  require(contains(request.address) && request.width == 4 && request.address % 4 == 0 &&
              !request.instruction,
          ErrorCode::InvalidOperand, "decoder MMIO requires aligned data words");
  auto &r = registers_[core];
  const auto offset = request.address - config_.base;
  if (request.write) {
    switch (offset) {
    case 0:
      (void)decoder(request.value);
      r.decoder = request.value;
      return 0;
    case 4:
      r.count = request.value;
      return 0;
    case 8:
      r.low = request.value;
      return 0;
    case 12:
      r.high = request.value;
      return 0;
    case 16:
      r.tag = request.value;
      return 0;
    case 20: {
      (void)decoder(r.decoder);
      require(request.value == 1 || request.value == 2 || request.value == 3,
              ErrorCode::InvalidOperand, "invalid decoder command");
      if (request.value == 3) {
        auto &s = sessions_[{core, r.decoder}];
        require(s.result.has_value() && !s.submitted && s.measurements.empty(), ErrorCode::Protocol,
                "decoder result is not ready");
        s = {};
        event(now, epoch, "DecoderResultConsumed", 0, core, r.decoder);
        return 0;
      }
      require(request.value == 2 || (r.count > 0 && r.count <= 64), ErrorCode::InvalidOperand,
              "decoder packet must contain 1 to 64 bits");
      if (requests_.size() >= config_.request_capacity)
        return std::nullopt;
      const auto bytes =
          checked_add(config_.packet_overhead, request.value == 2 ? 0 : (r.count + 7ULL) / 8);
      const auto duration =
          std::max<Tick>(1, (bytes + config_.bytes_per_tick - 1) / config_.bytes_per_tick);
      const auto sent = std::max(now, tx_available_);
      tx_available_ = checked_add(sent, duration);
      require(next_ != std::numeric_limits<Id>::max(), ErrorCode::Capacity,
              "decoder request id overflow");
      const auto id = next_++;
      requests_.push_back({id, core, r.decoder, r.count, request.value, r.tag,
                           std::uint64_t(r.low) | (std::uint64_t(r.high) << 32), sent,
                           checked_add(tx_available_, config_.link_latency)});
      event(now, epoch, "DecoderRequestSubmitted", id, core, r.decoder, r.tag);
      return 0;
    }
    default:
      throw Fault(ErrorCode::InvalidOperand, "read-only decoder register");
    }
  }
  const auto &d = decoder(r.decoder);
  if (offset == 4)
    return d.outputs;
  const auto &s = sessions_[{core, r.decoder}];
  if (offset == 20) {
    const bool pending = std::any_of(requests_.begin(), requests_.end(), [&](const auto &p) {
      return p.core == core && p.decoder == r.decoder;
    });
    return (s.result ? 1U : 0U) | ((pending || s.submitted || !s.measurements.empty()) ? 2U : 0U);
  }
  require((offset == 24 || offset == 28) && s.result.has_value(), ErrorCode::Protocol,
          "decoder result read before completion");
  return static_cast<std::uint32_t>(*s.result >> (offset == 24 ? 0 : 32));
}
void DecoderSystem::step(Tick now, Epoch epoch) {
  for (auto &p : requests_) {
    if (!p.transmitted && p.sent <= now) {
      event(now, epoch, "DecoderRequestSent", p.id, p.core, p.decoder, p.tag);
      p.transmitted = true;
    }
  }
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    auto &j = *it;
    if (!j.started && j.start <= now) {
      event(now, epoch, "DecoderStarted", j.id, j.core, j.decoder);
      j.started = true;
    }
    if (!j.completed && j.completion <= now) {
      event(now, epoch, "DecoderCompleted", j.id, j.core, j.decoder);
      j.completed = true;
    }
    if (j.arrival && *j.arrival <= now) {
      auto &s = sessions_.at({j.core, j.decoder});
      s.result = s.result.value_or(0) ^ j.result;
      s.submitted = false;
      event(now, epoch, "DecoderResultReturned", j.id, j.core, j.decoder);
      it = jobs_.erase(it);
    } else
      ++it;
  }
  while (!requests_.empty() && requests_.front().arrival <= now) {
    const auto &p = requests_.front();
    const auto &d = decoder(p.decoder);
    auto &s = sessions_[{p.core, p.decoder}];
    if (p.command == 2) {
      std::erase_if(jobs_,
                    [&](const auto &j) { return j.core == p.core && j.decoder == p.decoder; });
      s = {};
      event(now, epoch, "DecoderReset", p.id, p.core, p.decoder);
      requests_.pop_front();
      continue;
    }
    if (s.submitted)
      break;
    require(s.measurements.size() + p.count <= d.measurements, ErrorCode::Protocol,
            "decoder measurement count or session state mismatch");
    const auto reserved = static_cast<std::size_t>(
        std::count_if(sessions_.begin(), sessions_.end(), [](const auto &v) {
          return v.second.result.has_value() || v.second.submitted;
        }));
    if (s.measurements.size() + p.count == d.measurements && !s.result &&
        reserved >= config_.result_capacity)
      break;
    for (std::uint32_t bit = 0; bit < p.count; ++bit)
      s.measurements.push_back((p.data >> bit) & 1);
    event(now, epoch, "DecoderRequestArrived", p.id, p.core, p.decoder, p.tag);
    if (s.measurements.size() == d.measurements) {
      const auto result = d.decode(s.measurements);
      require(result.size() == d.outputs, ErrorCode::BackendFailure,
              "decoder returned an incorrect result width");
      std::uint64_t bits = 0;
      for (std::size_t bit = 0; bit < result.size(); ++bit)
        bits |= std::uint64_t(result[bit]) << bit;
      const auto start = std::max(checked_add(now, 1), available_[d.id]);
      available_[d.id] = checked_add(start, d.initiation_interval);
      const auto complete = checked_add(start, d.latency);
      jobs_.push_back({p.id, p.core, p.decoder, start, complete, bits, std::nullopt});
      s.submitted = true;
      s.measurements.clear();
    }
    requests_.pop_front();
  }
  if (rx_available_ <= now) {
    auto ready = jobs_.end();
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it)
      if (it->completed && !it->arrival &&
          (ready == jobs_.end() || it->completion < ready->completion ||
           (it->completion == ready->completion && it->id < ready->id)))
        ready = it;
    if (ready != jobs_.end()) {
      const auto bytes =
          checked_add(config_.packet_overhead, (decoder(ready->decoder).outputs + 7ULL) / 8);
      const auto duration =
          std::max<Tick>(1, (bytes + config_.bytes_per_tick - 1) / config_.bytes_per_tick);
      const auto end = checked_add(now, duration);
      ready->arrival = checked_add(end, config_.link_latency);
      rx_available_ = end;
    }
  }
}
void DecoderSystem::reset() {
  registers_.clear();
  sessions_.clear();
  requests_.clear();
  jobs_.clear();
  available_.clear();
  tx_available_ = rx_available_ = 0;
}
bool DecoderSystem::idle() const {
  return requests_.empty() && jobs_.empty() &&
         std::all_of(sessions_.begin(), sessions_.end(),
                     [](const auto &entry) { return entry.second.measurements.empty(); });
}
std::optional<Tick> DecoderSystem::next_boundary(Tick now) const {
  std::optional<Tick> next;
  const auto include = [&](Tick tick) {
    if (tick > now && (!next || tick < *next))
      next = tick;
  };
  for (const auto &p : requests_) {
    if (!p.transmitted)
      include(p.sent);
    include(p.arrival);
  }
  for (const auto &j : jobs_) {
    include(j.start);
    include(j.completion);
    if (j.arrival)
      include(*j.arrival);
    else if (j.completed)
      include(rx_available_);
  }
  return next;
}
} // namespace qsbit
