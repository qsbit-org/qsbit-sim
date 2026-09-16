#pragma once

#include "qsbit/time.hpp"
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace qsbit {
struct TraceEvent {
  TraceEvent(Tick at, Epoch session, std::string type, Id identity = 0, Id timing_label = 0,
             Tick local_cycle = 0)
      : tick(at), epoch(session), kind(std::move(type)), id(identity), label(timing_label),
        cycle(local_cycle) {}
  Tick tick = 0;
  Epoch epoch = 0;
  std::string kind;
  Id id = 0, label = 0;
  Tick cycle = 0;
  std::uint32_t port = 0, codeword = 0;
  std::uint32_t pc = 0, word = 0, rd = 0, next_pc = 0;
  std::vector<std::uint32_t> registers;
  std::vector<std::uint32_t> targets;
  std::string operation, detail;
  std::uint64_t value = 0;
};
class Trace {
public:
  void emit(TraceEvent event);
  [[nodiscard]] const std::vector<TraceEvent> &events() const { return events_; }
  void write_jsonl(std::ostream &stream) const;

private:
  std::vector<TraceEvent> events_;
};
} // namespace qsbit
