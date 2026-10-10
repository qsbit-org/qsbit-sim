#pragma once
#include "qsbit/cpu/pipeline.hpp"
namespace qsbit {
class CpuCycleModel final : public ICpuCycleModel {
public:
  CpuCycleModel(Clock clock, std::uint32_t entry, Trace &trace)
      : pipeline_(clock, entry, trace, ControlIssue::Scalar) {}
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
