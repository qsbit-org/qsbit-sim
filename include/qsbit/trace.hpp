#pragma once

#include "qsbit/time.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace qsbit {
struct CpuPipelineInstruction {
  Id id = 0;
  std::uint32_t pc = 0;
  std::optional<std::uint32_t> word;
  bool discarded = false;
  bool operator==(const CpuPipelineInstruction &) const = default;
};
struct CpuPipelineState {
  std::array<std::optional<CpuPipelineInstruction>, 4> stages;
  bool halted = false;
  bool operator==(const CpuPipelineState &) const = default;
};
enum class DecoderEventKind {
  RequestSubmitted,
  RequestSent,
  RequestArrived,
  Started,
  Completed,
  ResultReturned,
  Reset,
  ResultConsumed
};
struct DecoderTrace {
  std::uint32_t id, tag;
  std::size_t requests, resets, jobs;
};
struct DecoderEvent {
  DecoderEventKind kind;
  DecoderTrace state;
};
struct InstructionRetired {
  std::uint32_t pc, word, rd, next_pc, value;
  std::array<std::uint32_t, 32> registers;
};
struct TraceEvent {
  TraceEvent(Tick at, Epoch session, std::string type, Id identity = 0, Id timing_label = 0,
             Tick local_cycle = 0)
      : tick(at), epoch(session), kind(std::move(type)), id(identity), label(timing_label),
        cycle(local_cycle) {}
  TraceEvent(Tick at, Epoch session, Tick local_cycle, CpuPipelineState state)
      : tick(at), epoch(session), kind("CpuPipelineUpdated"), cycle(local_cycle),
        payload_(std::move(state)) {}
  TraceEvent(Tick at, Epoch session, Id instruction, Tick local_cycle, InstructionRetired state)
      : tick(at), epoch(session), kind("InstructionRetired"), id(instruction), cycle(local_cycle),
        payload_(std::move(state)) {}
  TraceEvent(Tick at, Epoch session, Id request, std::uint32_t core_id, DecoderEvent event);
  template <typename T> [[nodiscard]] const T *get_if() const { return std::get_if<T>(&payload_); }
  Tick tick = 0;
  Epoch epoch = 0;
  std::string kind;
  Id id = 0, label = 0;
  Tick cycle = 0;
  std::uint32_t port = 0, codeword = 0;
  std::vector<std::uint32_t> targets;
  std::string operation, detail;
  std::uint64_t value = 0;
  std::optional<std::uint32_t> core;

private:
  std::variant<std::monostate, CpuPipelineState, InstructionRetired, DecoderEvent> payload_;
};
class Trace {
public:
  Trace() = default;
  Trace(Trace &destination, std::optional<std::uint32_t> core)
      : destination_(&destination), core_(core) {}
  void emit(TraceEvent event);
  void include_stalls(bool enabled) { include_stalls_ = enabled; }
  [[nodiscard]] const std::vector<TraceEvent> &events() const { return events_; }
  void write_jsonl(std::ostream &stream) const;

private:
  std::vector<TraceEvent> events_;
  bool include_stalls_ = true;
  Trace *destination_ = nullptr;
  std::optional<std::uint32_t> core_;
};
} // namespace qsbit
