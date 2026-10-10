#include "qsbit/cpu/rv32.hpp"
#include "qsbit/cpu/vliw.hpp"
#include "qsbit/defaults.hpp"
#include "test.hpp"

using namespace qsbit;
namespace {
void decoder() {
  std::array<std::uint32_t, 32> registers{};
  registers[1] = 0xffffffff;
  registers[2] = 0x12345678;
  registers[31] = 0x87654321;
  for (unsigned first = 0; first < 4; ++first)
    for (unsigned second = 0; second < 4; ++second) {
      const auto word = 0x2bU | ((1U | (2U << 5) | (first << 10)) << 7) |
                        ((31U | (31U << 5) | (second << 10)) << 19);
      const auto operations = decode_cw_bundle(word, 7, registers);
      CHECK((operations[0] == ControlOperation{7, CodewordCommand{first & 1 ? 1U : 0xffffffffU,
                                                                  first & 2 ? 2U : 0x12345678U}}));
      CHECK(
          (operations[1] == ControlOperation{7, CodewordCommand{second & 1 ? 31U : 0x87654321U,
                                                                second & 2 ? 31U : 0x87654321U}}));
    }
  for (auto word : {0x8000002bU, 0x0bU, 0xffffffffU})
    faults(ErrorCode::IllegalInstruction, [&] { decode_cw_bundle(word, 1, registers); });
  faults(ErrorCode::IllegalInstruction, [] { (void)rv32::decode(0x610e102b); });
}
ProgramImage image() {
  // Two immediate codewords, then the scalar exit sequence.
  const std::array<std::uint32_t, 4> words{0x610e102b, 0x00000513, 0x05d00893, 0x00000073};
  std::vector<std::uint8_t> bytes;
  for (auto word : words)
    for (unsigned shift = 0; shift < 32; shift += 8)
      bytes.push_back(static_cast<std::uint8_t>(word >> shift));
  return ProgramImage::raw(bytes, 0, 0, 4096);
}
void blocked_lane(bool reset) {
  const auto clock = default_profile().cpu;
  Trace trace;
  MemoryPort fetch{clock}, data{clock};
  MemoryModel memory(image(), clock, 1);
  VliwCpuCycleModel cpu(clock, 0, trace);
  unsigned first = 0, second = 0, stalls = 3, exits = 0;
  Epoch epoch = 1;
  CpuPorts ports{fetch, data, [&](const ControlOperation &op) -> std::optional<std::uint32_t> {
                   if (op.is<HaltCommand>()) {
                     ++exits;
                   } else {
                     CHECK(op.is<CodewordCommand>() && op.get<CodewordCommand>().codeword == 1);
                     if (op.get<CodewordCommand>().port == 0) {
                       ++first;
                     } else {
                       CHECK(op.get<CodewordCommand>().port == 1);
                       if (stalls) {
                         --stalls;
                         return std::nullopt;
                       }
                       ++second;
                     }
                   }
                   return 0;
                 }};
  for (Tick now = 0; now < 2000 && !cpu.halted(); now += clock.period) {
    cpu.step(now, epoch, ports);
    memory.step(now, epoch, fetch, data);
    if (reset && stalls == 2 && epoch == 1) {
      cpu.reset(4);
      fetch.reset();
      data.reset();
      memory.reset();
      epoch = 2;
    }
  }
  CHECK(cpu.halted() && first == 1 && second == (reset ? 0U : 1U) && exits == 1);
  CHECK(cpu.pc() == 16 && cpu.registers()[0] == 0);
}
} // namespace
int main() {
  try {
    decoder();
    blocked_lane(false);
    blocked_lane(true);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
