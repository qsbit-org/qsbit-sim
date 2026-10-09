#include "qsbit/cpu/rv32.hpp"
#include "qsbit/cpu/vliw.hpp"
#include "test.hpp"
#include <algorithm>

using namespace qsbit;
namespace {
template <typename Cpu> void pipeline(std::uint32_t exit_code) {
  const Clock clock{5, 2};
  const std::array<std::uint32_t, 9> words{0x00100093,
                                           0x0610000b,
                                           0x00200113,
                                           0x0080006f,
                                           0x06300093,
                                           0x00300193,
                                           (exit_code << 20) | 0x00000513U,
                                           0x05d00893,
                                           0x00000073};
  std::vector<std::uint8_t> bytes;
  for (auto word : words)
    for (unsigned shift = 0; shift < 32; shift += 8)
      bytes.push_back(static_cast<std::uint8_t>(word >> shift));
  Trace destination;
  Trace trace(destination, 7);
  MemoryPort fetch{clock}, data{clock};
  MemoryModel memory(ProgramImage::raw(bytes, 0, 0, 4096), clock, 1);
  Cpu cpu(clock, 0, trace);
  unsigned stalls = 8;
  CpuPorts ports{fetch, data, [&](const ControlOperation &op) -> std::optional<std::uint32_t> {
                   if (op.kind == ControlKind::Codeword && stalls) {
                     --stalls;
                     return std::nullopt;
                   }
                   return 0;
                 }};
  Tick now = clock.phase;
  for (; now < 2000 && !cpu.halted(); now += clock.period) {
    cpu.step(now, 1, ports);
    memory.step(now, 1, fetch, data);
  }
  CHECK(cpu.halted() && cpu.registers()[1] == 1 && cpu.registers()[3] == 3);
  CHECK(cpu.registers()[10] == exit_code);
  std::optional<CpuPipelineState> previous;
  std::array<bool, 4> occupied{};
  Tick decoded = 0, executed = 0, retired = 0;
  bool held = false, flushed = false, halted = false, pending_flush = false;
  for (const auto &event : destination.events()) {
    CHECK(event.core == 7);
    if (event.kind == "InstructionRetired") {
      CHECK(event.pc != 16);
      if (event.pc == 0)
        retired = event.tick;
    }
    if (event.kind == "PipelineFlushed") {
      flushed = true;
      pending_flush = true;
    }
    if (event.kind != "CpuPipelineUpdated")
      continue;
    CHECK(event.pipeline && event.cycle == (event.tick - clock.phase) / clock.period);
    const auto &state = *event.pipeline;
    if (pending_flush) {
      CHECK(!state.stages[1] && !state.stages[2] && !state.stages[3]);
      pending_flush = false;
    }
    CHECK(!previous || state != *previous);
    previous = state;
    for (std::size_t stage = 0; stage < state.stages.size(); ++stage) {
      const auto &slot = state.stages[stage];
      if (!slot)
        continue;
      occupied[stage] = true;
      if (stage == 0) {
        CHECK(!slot->word);
      } else {
        CHECK(slot->word && slot->pc / 4 < words.size());
        CHECK(*slot->word == words[slot->pc / 4]);
      }
      if (slot->pc == 0 && stage == 2)
        decoded = event.tick;
      if (slot->pc == 0 && stage == 3)
        executed = event.tick;
    }
    if (state.stages[3] && state.stages[3]->pc == 4 && state.stages[1] && state.stages[2])
      held = true;
    if (state.halted) {
      CHECK(!state.stages[1] && !state.stages[2] && !state.stages[3]);
      CHECK(!state.stages[0] || state.stages[0]->discarded);
      halted = true;
    }
  }
  CHECK(std::ranges::all_of(occupied, [](bool seen) { return seen; }));
  CHECK(decoded < executed && executed < retired);
  CHECK(held && flushed && halted);

  cpu.reset(0);
  fetch.reset();
  data.reset();
  memory.reset();
  cpu.step(now, 2, ports);
  const auto &reset = destination.events().back();
  CHECK(reset.kind == "CpuPipelineUpdated" && reset.epoch == 2 && reset.pipeline);
  CHECK(!reset.pipeline->halted && reset.pipeline->stages[0]->id == 1);
  CHECK(!reset.pipeline->stages[1] && !reset.pipeline->stages[2] && !reset.pipeline->stages[3]);
}
} // namespace
int main() {
  try {
    for (auto status : {0U, 1U, 2U, 63U}) {
      pipeline<CpuCycleModel>(status);
      pipeline<VliwCpuCycleModel>(status);
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
