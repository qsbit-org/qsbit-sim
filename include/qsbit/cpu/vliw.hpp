#pragma once
#include "qsbit/cpu/pipeline.hpp"
namespace qsbit {
// Word: opcode 0x2b in bits 6:0, two 12-bit slots in bits 30:7, bit 31 zero.
// Slot: port in bits 4:0, codeword in bits 9:5, immediate flags in bits 11:10.
std::array<ControlOperation, 2> decode_cw_bundle(std::uint32_t word, Id instruction,
                                                 const std::array<std::uint32_t, 32> &registers);

class VliwCpuCycleModel final : public ICpuCycleModel {
public:
  VliwCpuCycleModel(Clock clock, std::uint32_t entry, Trace &trace)
      : pipeline_(clock, entry, trace, ControlIssue::DualCodeword) {}
  void step(Tick now, Epoch epoch, CpuPorts &ports) override { pipeline_.step(now, epoch, ports); }
  void reset(std::uint32_t entry) override { pipeline_.reset(entry); }
  [[nodiscard]] const std::array<std::uint32_t, 32> &registers() const override {
    return pipeline_.registers();
  }
  [[nodiscard]] std::uint32_t pc() const override { return pipeline_.pc(); }
  [[nodiscard]] bool halted() const override { return pipeline_.halted(); }

private:
  InOrderPipeline pipeline_;
};
} // namespace qsbit
