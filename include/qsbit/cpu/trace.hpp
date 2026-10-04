#pragma once

#include "qsbit/trace.hpp"

namespace qsbit {
class CpuPipelineTrace {
public:
  explicit CpuPipelineTrace(Trace &trace) : trace_(trace) {}
  void reset() { previous_.reset(); }

  template <typename Fetch, typename Frame>
  void sample(Tick now, Epoch epoch, Clock clock, const std::optional<Fetch> &fetch,
              const std::optional<Frame> &fetched, const std::optional<Frame> &decode,
              const std::optional<Frame> &execute, Id generation, bool halted) {
    CpuPipelineState state;
    if (fetch)
      state.stages[0] = CpuPipelineInstruction{fetch->id, fetch->pc, std::nullopt,
                                               fetch->generation != generation || halted};
    const std::array frames{&fetched, &decode, &execute};
    for (std::size_t i = 0; i < frames.size(); ++i)
      if (*frames[i]) {
        const auto &frame = **frames[i];
        state.stages[i + 1] = CpuPipelineInstruction{frame.id, frame.pc, frame.word, false};
      }
    state.halted = halted;
    if (previous_ && epoch == epoch_ && state == *previous_)
      return;
    TraceEvent event{now, epoch, "CpuPipelineUpdated", 0, 0, (now - clock.phase) / clock.period};
    event.pipeline = state;
    trace_.emit(std::move(event));
    previous_ = std::move(state);
    epoch_ = epoch;
  }

private:
  Trace &trace_;
  std::optional<CpuPipelineState> previous_;
  Epoch epoch_ = 0;
};
} // namespace qsbit
