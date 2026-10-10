#include "qsbit/decoder.hpp"
#include "qsbit/contracts/decoder.hpp"
#include "qsbit/decoder/mmio.hpp"
#include "qsbit/error.hpp"
#include "qsbit/memory.hpp"
#include "qsbit/time.hpp"
#include "qsbit/trace.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <variant>

namespace qsbit {
namespace registers = contract::decoder;
namespace {
std::set<std::uint32_t> decoder_ids(const DecoderSystemConfig &config) {
  std::set<std::uint32_t> ids;
  for (const auto &decoder : config.decoders)
    ids.insert(decoder.id);
  return ids;
}
} // namespace
DecoderSystem::DecoderSystem(DecoderSystemConfig config, Trace &trace)
    : config_(std::move(config)), trace_(trace), mmio_(config_.base, decoder_ids(config_)) {
  require(config_.request_capacity > 0 && config_.result_capacity > 0 &&
              config_.bytes_per_tick > 0 && config_.link_latency > 0,
          ErrorCode::InvalidProfile, "invalid decoder transport configuration");
  std::set<std::uint32_t> ids;
  for (const auto &d : config_.decoders)
    require(ids.insert(d.id).second && d.measurements > 0 && d.measurements <= 1048576 &&
                d.outputs > 0 && d.outputs <= 64 && d.latency > 0 && d.initiation_interval > 0 &&
                bool(d.decode),
            ErrorCode::InvalidProfile, "invalid decoder configuration");
}
const DecoderConfig &DecoderSystem::decoder(std::uint32_t id) const {
  const auto it = std::find_if(config_.decoders.begin(), config_.decoders.end(),
                               [id](const auto &d) { return d.id == id; });
  require(it != config_.decoders.end(), ErrorCode::InvalidOperand, "unknown decoder id");
  return *it;
}
void DecoderSystem::event(Tick now, Epoch epoch, DecoderEventKind kind, Id id, std::uint32_t core,
                          std::uint32_t target, std::uint32_t tag) {
  trace_.emit_decoder(now, epoch, kind, id, core,
                      {target, tag, requests_.size(),
                       static_cast<std::size_t>(reset_request_.has_value()), jobs_.size()});
}
std::optional<std::uint32_t> DecoderSystem::access(std::uint32_t core, const MemoryRequest &request,
                                                   Tick now, Epoch epoch) {
  const auto command = mmio_.access(core, request);
  if (!command)
    return 0;
  if (const auto *consume = std::get_if<DecoderConsume>(&*command)) {
    auto &session = sessions_[{core, consume->decoder}];
    require(session.result.has_value() && !session.submitted && session.measurements.empty(),
            ErrorCode::Protocol, "decoder result is not ready");
    session = {};
    event(now, epoch, DecoderEventKind::ResultConsumed, 0, core, consume->decoder);
    return 0;
  }
  if (const auto *read = std::get_if<DecoderRead>(&*command)) {
    if (read->kind == DecoderReadKind::Count)
      return decoder(read->decoder).outputs;
    const auto &session = sessions_[{core, read->decoder}];
    if (read->kind == DecoderReadKind::Status) {
      const auto matches = [&](const auto &p) {
        return p.core == core && p.decoder == read->decoder;
      };
      const bool pending = (reset_request_ && matches(*reset_request_)) ||
                           std::any_of(requests_.begin(), requests_.end(), matches);
      return (session.result ? registers::Ready : 0U) |
             ((pending || session.submitted || !session.measurements.empty()) ? registers::Busy
                                                                              : 0U);
    }
    require(session.result.has_value(), ErrorCode::Protocol,
            "decoder result read before completion");
    return static_cast<std::uint32_t>(*session.result >>
                                      (read->kind == DecoderReadKind::Low ? 0 : 32));
  }
  const auto *reset = std::get_if<DecoderReset>(&*command);
  const auto submission =
      reset ? DecoderSubmit{reset->decoder, 0, reset->tag, 0} : std::get<DecoderSubmit>(*command);
  if (reset ? reset_request_.has_value() : requests_.size() >= config_.request_capacity)
    return std::nullopt;
  const auto bytes = checked_add(config_.packet_overhead, (submission.count + 7ULL) / 8);
  const auto duration =
      std::max<Tick>(1, (bytes + config_.bytes_per_tick - 1) / config_.bytes_per_tick);
  const auto sent = std::max(now, tx_available_);
  const auto end = checked_add(sent, duration);
  const auto arrival = checked_add(end, config_.link_latency);
  require(next_ != std::numeric_limits<Id>::max(), ErrorCode::Capacity,
          "decoder request id overflow");
  const auto id = next_;
  const Request packet{
      id,   core,   submission.decoder, submission.count, submission.tag, submission.data,
      sent, arrival};
  if (reset)
    reset_request_ = packet;
  else
    requests_.push_back(packet);
  ++next_;
  tx_available_ = end;
  event(now, epoch, DecoderEventKind::RequestSubmitted, id, core, submission.decoder,
        submission.tag);
  return 0;
}
void DecoderSystem::step(Tick now, Epoch epoch) {
  advance_transmission(now, epoch);
  advance_jobs(now, epoch);
  admit_requests(now, epoch);
  transmit_result(now);
}
void DecoderSystem::advance_transmission(Tick now, Epoch epoch) {
  const auto transmitted = [&](Request &p) {
    if (!p.transmitted && p.sent <= now) {
      event(now, epoch, DecoderEventKind::RequestSent, p.id, p.core, p.decoder, p.tag);
      p.transmitted = true;
    }
  };
  for (auto &p : requests_)
    transmitted(p);
  if (reset_request_) {
    transmitted(*reset_request_);
    if (reset_request_->arrival <= now) {
      const auto p = *reset_request_;
      reset_request_.reset();
      const auto cancelled = [&](const auto &item) {
        return item.core == p.core && item.decoder == p.decoder && item.id < p.id;
      };
      std::erase_if(requests_, cancelled);
      std::erase_if(jobs_, cancelled);
      sessions_[{p.core, p.decoder}] = {};
      event(now, epoch, DecoderEventKind::Reset, p.id, p.core, p.decoder, p.tag);
    }
  }
}
void DecoderSystem::advance_jobs(Tick now, Epoch epoch) {
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    auto &j = *it;
    if (!j.started && j.start <= now) {
      event(now, epoch, DecoderEventKind::Started, j.id, j.core, j.decoder);
      j.started = true;
    }
    if (!j.completed && j.completion <= now) {
      event(now, epoch, DecoderEventKind::Completed, j.id, j.core, j.decoder);
      j.completed = true;
    }
    if (j.arrival && *j.arrival <= now) {
      auto &s = sessions_.at({j.core, j.decoder});
      s.result = s.result.value_or(0) ^ j.result;
      s.submitted = false;
      event(now, epoch, DecoderEventKind::ResultReturned, j.id, j.core, j.decoder);
      it = jobs_.erase(it);
    } else
      ++it;
  }
}
void DecoderSystem::admit_requests(Tick now, Epoch epoch) {
  while (!requests_.empty() && requests_.front().arrival <= now) {
    const auto &p = requests_.front();
    const auto &d = decoder(p.decoder);
    auto &s = sessions_[{p.core, p.decoder}];
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
    event(now, epoch, DecoderEventKind::RequestArrived, p.id, p.core, p.decoder, p.tag);
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
}
void DecoderSystem::transmit_result(Tick now) {
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
  mmio_.reset();
  sessions_.clear();
  requests_.clear();
  reset_request_.reset();
  jobs_.clear();
  available_.clear();
  tx_available_ = rx_available_ = 0;
}
bool DecoderSystem::idle() const {
  return requests_.empty() && !reset_request_ && jobs_.empty() &&
         std::all_of(sessions_.begin(), sessions_.end(),
                     [](const auto &entry) { return entry.second.measurements.empty(); });
}
std::optional<Tick> DecoderSystem::next_boundary(Tick now) const {
  std::optional<Tick> next;
  const auto include = [&](Tick tick) {
    if (tick > now && (!next || tick < *next))
      next = tick;
  };
  const auto request_boundary = [&](const Request &p) {
    if (!p.transmitted)
      include(p.sent);
    include(p.arrival);
  };
  for (const auto &p : requests_)
    request_boundary(p);
  if (reset_request_)
    request_boundary(*reset_request_);
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
