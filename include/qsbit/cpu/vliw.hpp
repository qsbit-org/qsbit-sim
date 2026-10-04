#pragma once

#include "qsbit/cpu.hpp"
#include "qsbit/cpu/trace.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/trace.hpp"

namespace qsbit {
// Word: opcode 0x2b in bits 6:0, two 12-bit slots in bits 30:7, bit 31 zero.
// Slot: port in bits 4:0, codeword in bits 9:5, immediate flags in bits 11:10.
std::array<ControlOperation, 2> decode_cw_bundle(std::uint32_t word, Id instruction,
                                                 const std::array<std::uint32_t, 32> &registers);

class VliwCpuCycleModel final : public ICpuCycleModel {
public:
  VliwCpuCycleModel(Clock clock, std::uint32_t entry, Trace &trace);
  void step(Tick now, Epoch epoch, CpuPorts &ports) override;
  void reset(std::uint32_t entry) override;
  [[nodiscard]] const std::array<std::uint32_t, 32> &registers() const override {
    return registers_;
  }
  [[nodiscard]] std::uint32_t pc() const override { return pc_; }
  [[nodiscard]] bool halted() const override { return halted_; }

private:
  struct Fetch {
    Id id;
    std::uint32_t pc;
    Id generation;
  };
  struct Frame {
    Id id = 0;
    std::uint32_t pc = 0, word = 0, lhs = 0, rhs = 0;
    std::optional<ErrorCode> fault;
    std::optional<rv32::Decoded> decoded;
    std::optional<rv32::Effect> effect;
    Id memory_request = 0;
    bool bundle = false;
    std::array<ControlOperation, 2> operations{};
    std::size_t completed_lanes = 0;
  };
  void retire(const Frame &frame, std::uint32_t value, std::uint32_t next_pc, Tick now,
              Epoch epoch);
  Clock clock_;
  Trace &trace_;
  CpuPipelineTrace pipeline_trace_;
  std::array<std::uint32_t, 32> registers_{};
  std::uint32_t pc_ = 0, fetch_pc_ = 0;
  std::optional<Fetch> fetch_request_;
  std::optional<Frame> fetched_, decode_, execute_;
  Id next_fetch_ = 1, next_data_ = 1, generation_ = 0;
  bool halted_ = false;
};
} // namespace qsbit
