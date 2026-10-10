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
} // namespace
void Trace::emit_decoder(Tick tick, Epoch epoch, DecoderEventKind kind, Id request,
                         std::uint32_t core, DecoderTrace state) {
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
  TraceEvent event{tick, epoch, name, request};
  event.core = core;
  event.decoder = state;
  emit(std::move(event));
}
void Trace::emit(TraceEvent event) {
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
    out << "{\"schema\":1,\"tick\":" << e.tick << ",\"epoch\":" << e.epoch
        << ",\"kind\":" << quoted(e.kind) << ",\"id\":" << e.id << ",\"label\":" << e.label
        << ",\"cycle\":" << e.cycle << ",\"port\":" << e.port << ",\"codeword\":" << e.codeword
        << ",\"pc\":" << e.pc << ",\"word\":" << e.word << ",\"rd\":" << e.rd
        << ",\"next_pc\":" << e.next_pc << ",\"operation\":" << quoted(e.operation)
        << ",\"detail\":" << quoted(e.detail) << ",\"value\":" << e.value << ",\"targets\":[";
    for (std::size_t i = 0; i < e.targets.size(); ++i) {
      if (i)
        out << ',';
      out << e.targets[i];
    }
    out << "],\"registers\":[";
    for (std::size_t i = 0; i < e.registers.size(); ++i) {
      if (i)
        out << ',';
      out << e.registers[i];
    }
    out << ']';
    if (e.core)
      out << ",\"core\":" << *e.core;
    if (e.decoder) {
      const auto &d = *e.decoder;
      out << ",\"decoder\":{\"id\":" << d.id << ",\"tag\":" << d.tag
          << ",\"requests\":" << d.requests << ",\"resets\":" << d.resets << ",\"jobs\":" << d.jobs
          << '}';
    }
    if (e.pipeline) {
      out << ",\"pipeline\":{\"halted\":" << (e.pipeline->halted ? "true" : "false");
      const std::array names{"fetch", "fetched", "decode", "execute"};
      for (std::size_t i = 0; i < names.size(); ++i) {
        out << ',' << quoted(names[i]) << ':';
        const auto &stage = e.pipeline->stages[i];
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
