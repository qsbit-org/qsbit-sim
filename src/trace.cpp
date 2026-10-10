#include "qsbit/trace.hpp"
#include "qsbit/error.hpp"
#include "qsbit/time.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace qsbit {
namespace {
std::string quoted(const std::string &s) {
  std::ostringstream out;
  out << '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\')
      out << '\\' << ch;
    else if (c < 0x20)
      out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    else
      out << ch;
  }
  out << '"';
  return out.str();
}
const char *decoder_name(DecoderEventKind kind) {
  const char *name = nullptr;
  switch (kind) {
  case DecoderEventKind::RequestSubmitted:
    name = "DecoderRequestSubmitted";
    break;
  case DecoderEventKind::RequestSent:
    name = "DecoderRequestSent";
    break;
  case DecoderEventKind::RequestArrived:
    name = "DecoderRequestArrived";
    break;
  case DecoderEventKind::Started:
    name = "DecoderStarted";
    break;
  case DecoderEventKind::Completed:
    name = "DecoderCompleted";
    break;
  case DecoderEventKind::ResultReturned:
    name = "DecoderResultReturned";
    break;
  case DecoderEventKind::Reset:
    name = "DecoderReset";
    break;
  case DecoderEventKind::ResultConsumed:
    name = "DecoderResultConsumed";
    break;
  }
  require(name != nullptr, ErrorCode::Protocol, "invalid decoder trace kind");
  return name;
}
} // namespace
TraceEvent::TraceEvent(Tick at, Epoch session, Id request, std::uint32_t core_id,
                       DecoderEvent event)
    : tick(at), epoch(session), kind(decoder_name(event.kind)), id(request), core(core_id),
      payload_(event) {}
void Trace::emit(TraceEvent event) {
  require((event.kind == "InstructionRetired") == (event.get_if<InstructionRetired>() != nullptr) &&
              (event.kind == "CpuPipelineUpdated") ==
                  (event.get_if<CpuPipelineState>() != nullptr) &&
              event.kind.starts_with("Decoder") == (event.get_if<DecoderEvent>() != nullptr),
          ErrorCode::Protocol, "trace kind and payload differ");
  if (const auto *decoder = event.get_if<DecoderEvent>())
    require(event.kind == decoder_name(decoder->kind), ErrorCode::Protocol,
            "decoder trace kind and payload differ");
  if (destination_) {
    event.core = core_;
    destination_->emit(std::move(event));
    return;
  }
  if (!include_stalls_ && event.kind == "CpuStalled")
    return;
  require(events_.empty() || event.tick >= events_.back().tick, ErrorCode::Protocol,
          "trace time decreased");
  events_.push_back(std::move(event));
}
void Trace::write_jsonl(std::ostream &out) const {
  for (const auto &e : events_) {
    const auto *retired = e.get_if<InstructionRetired>();
    const auto *pipeline = e.get_if<CpuPipelineState>();
    const auto *decoder = e.get_if<DecoderEvent>();
    out << "{\"schema\":1,\"tick\":" << e.tick << ",\"epoch\":" << e.epoch
        << ",\"kind\":" << quoted(e.kind) << ",\"id\":" << e.id << ",\"label\":" << e.label
        << ",\"cycle\":" << e.cycle << ",\"port\":" << e.port << ",\"codeword\":" << e.codeword
        << ",\"pc\":" << (retired ? retired->pc : 0)
        << ",\"word\":" << (retired ? retired->word : 0)
        << ",\"rd\":" << (retired ? retired->rd : 0)
        << ",\"next_pc\":" << (retired ? retired->next_pc : 0)
        << ",\"operation\":" << quoted(e.operation) << ",\"detail\":" << quoted(e.detail)
        << ",\"value\":" << (retired ? retired->value : e.value) << ",\"targets\":[";
    for (std::size_t i = 0; i < e.targets.size(); ++i) {
      if (i)
        out << ',';
      out << e.targets[i];
    }
    out << "],\"registers\":[";
    for (std::size_t i = 0; retired && i < retired->registers.size(); ++i) {
      if (i)
        out << ',';
      out << retired->registers[i];
    }
    out << ']';
    if (e.core)
      out << ",\"core\":" << *e.core;
    if (decoder) {
      const auto &d = decoder->state;
      out << ",\"decoder\":{\"id\":" << d.id << ",\"tag\":" << d.tag
          << ",\"requests\":" << d.requests << ",\"resets\":" << d.resets << ",\"jobs\":" << d.jobs
          << '}';
    }
    if (pipeline) {
      out << ",\"pipeline\":{\"halted\":" << (pipeline->halted ? "true" : "false");
      const std::array names{"fetch", "fetched", "decode", "execute"};
      for (std::size_t i = 0; i < names.size(); ++i) {
        out << ',' << quoted(names[i]) << ':';
        const auto &stage = pipeline->stages[i];
        if (!stage) {
          out << "null";
          continue;
        }
        out << "{\"id\":" << stage->id << ",\"pc\":" << stage->pc << ",\"word\":";
        if (stage->word)
          out << *stage->word;
        else
          out << "null";
        out << ",\"discarded\":" << (stage->discarded ? "true" : "false") << '}';
      }
      out << '}';
    }
    out << "}\n";
  }
  require(bool(out), ErrorCode::Protocol, "trace output failed");
}
} // namespace qsbit
