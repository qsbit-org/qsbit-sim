#include "qsbit/trace.hpp"
#include <iomanip>
#include <ostream>
#include <sstream>
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
void Trace::emit(TraceEvent event) {
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
    out << "]}\n";
  }
  require(bool(out), ErrorCode::Protocol, "trace output failed");
}
} // namespace qsbit
