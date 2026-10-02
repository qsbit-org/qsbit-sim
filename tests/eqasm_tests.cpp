#include "qsbit/cpu/eqasm.hpp"
#include "qsbit/defaults.hpp"
#include "test.hpp"

using namespace qsbit;
namespace {
ProgramImage image(const std::vector<std::uint32_t> &words) {
  std::vector<std::uint8_t> bytes;
  for (auto word : words)
    for (unsigned shift = 0; shift < 32; shift += 8)
      bytes.push_back(static_cast<std::uint8_t>(word >> shift));
  return ProgramImage::raw(bytes, 0, 0, 4096);
}
EqasmConfiguration configuration() {
  return {{{0, 1}}, {{1, {0}, 0, 1}, {1, {1}, 1, 1}, {2, {0, 1}, 0, 6}}};
}
struct Harness {
  Profile profile = default_profile();
  Trace trace;
  MemoryPort fetch{profile.cpu}, data{profile.cpu};
  MemoryModel memory;
  EqasmCpuCycleModel cpu;
  std::vector<ControlOperation> accepted;
  std::optional<ControlOperation> held;
  unsigned stalls = 0;
  Tick now = 0;
  Epoch epoch = 1;
  explicit Harness(std::vector<std::uint32_t> words, EqasmConfiguration config = configuration())
      : memory(image(words), profile.cpu, 1), cpu(profile, 0, trace, config) {}
  void step() {
    CpuPorts ports{fetch, data, [&](const ControlOperation &op) -> std::optional<std::uint32_t> {
                     if (held)
                       CHECK(*held == op);
                     if (stalls) {
                       --stalls;
                       held = op;
                       return std::nullopt;
                     }
                     held.reset();
                     accepted.push_back(op);
                     return op.kind == ControlKind::FetchMeasurement ? 1 : 0;
                   }};
    cpu.step(now, epoch, ports);
    memory.step(now, epoch, fetch, data);
    now += profile.cpu.period;
  }
  void run() {
    for (unsigned i = 0; i < 10000 && !cpu.halted(); ++i)
      step();
    CHECK(cpu.halted());
  }
};
void instructions() {
  Harness h({0x40000001, 0x40100002, 0x40200003, 0x50000001, 0x2c0fffff, 0x2e100001, 0x70000000,
             0x8040010b, 0x60000005, 0x80440000, 0x60000005, 0x80800000, 0x2a200001, 0x00000000,
             0x10000000});
  h.stalls = 5;
  h.run();
  CHECK(h.cpu.pc() == 56);
  CHECK(h.cpu.registers()[0] == 0xffffffff);
  CHECK(h.cpu.registers()[1] == 0x3ffff);
  CHECK(h.cpu.registers()[2] == 1);
  const std::vector<ControlOperation> expected{{7, ControlKind::Wait, 0xfffff},
                                               {8, ControlKind::Wait, 3},
                                               {8, ControlKind::Codeword, 0, 1},
                                               {8, ControlKind::Codeword, 1, 1},
                                               {9, ControlKind::Wait, 5},
                                               {10, ControlKind::Codeword, 0, 1},
                                               {10, ControlKind::Codeword, 1, 1},
                                               {11, ControlKind::Wait, 5},
                                               {12, ControlKind::Codeword, 0, 6},
                                               {13, ControlKind::FetchMeasurement, 1},
                                               {15, ControlKind::Halt}};
  CHECK(h.accepted == expected);
  std::size_t retired = 0;
  for (const auto &event : h.trace.events())
    if (event.kind == "InstructionRetired")
      ++retired;
  CHECK(retired == 15);
}
void stalled_lane() {
  Harness h({0x40000001, 0x40100002, 0x80400108, 0x10000000});
  bool second_blocked = false;
  unsigned count = 0;
  while (!h.cpu.halted() && h.now < 2000) {
    CpuPorts ports{h.fetch, h.data,
                   [&](const ControlOperation &op) -> std::optional<std::uint32_t> {
                     if (op.kind == ControlKind::Codeword) {
                       if (op.first == 1 && !second_blocked) {
                         second_blocked = true;
                         return std::nullopt;
                       }
                       ++count;
                     }
                     return 0;
                   }};
    h.cpu.step(h.now, 1, ports);
    h.memory.step(h.now, 1, h.fetch, h.data);
    h.now += h.profile.cpu.period;
  }
  CHECK(h.cpu.halted() && second_blocked && count == 2);
}
void invalid() {
  for (auto word : {0x40000080U, 0x60000000U | (1U << 20), 0x10000001U, 0x00000013U, 0xffffffffU}) {
    Harness h({word});
    faults(ErrorCode::IllegalInstruction, [&] { h.run(); });
    CHECK(h.accepted.empty());
  }
  for (auto word : {0x40000004U, 0x50000002U, 0x2a000002U}) {
    Harness h({word});
    faults(ErrorCode::InvalidOperand, [&] { h.run(); });
    CHECK(h.accepted.empty());
  }
  Harness collision({0x40000001, 0x80400101});
  faults(ErrorCode::ResourceConflict, [&] { collision.run(); });
  CHECK(collision.accepted.empty());
  Harness same_time({0x40000001, 0x80400000, 0x80400000});
  faults(ErrorCode::ResourceConflict, [&] { same_time.run(); });
  CHECK(same_time.accepted.size() == 1);
  auto partial = configuration();
  partial.microcode.erase(partial.microcode.begin() + 1);
  Harness missing({0x40000002, 0x80400000}, partial);
  faults(ErrorCode::InvalidOperand, [&] { missing.run(); });
  CHECK(missing.accepted.empty());
}
void reset() {
  Harness h({0x40000001, 0x2c000017, 0x80400001, 0x10000000});
  h.stalls = 100;
  while (!h.held)
    h.step();
  CHECK(h.cpu.registers()[0] == 23);
  h.cpu.reset(8);
  h.fetch.reset();
  h.data.reset();
  h.memory.reset();
  h.held.reset();
  h.stalls = 0;
  h.epoch = 2;
  CHECK(h.cpu.pc() == 8 && h.cpu.registers()[0] == 0 && !h.cpu.halted());
  h.run();
  CHECK(h.accepted ==
        (std::vector<ControlOperation>{{1, ControlKind::Wait, 1}, {2, ControlKind::Halt}}));
}
void config() {
  const auto profile = default_profile();
  Trace trace;
  auto duplicate = configuration();
  duplicate.microcode.push_back(duplicate.microcode[0]);
  faults(ErrorCode::InvalidProfile, [&] { EqasmCpuCycleModel cpu(profile, 0, trace, duplicate); });
  auto mismatch = configuration();
  mismatch.microcode[0].targets = {1};
  faults(ErrorCode::InvalidProfile, [&] { EqasmCpuCycleModel cpu(profile, 0, trace, mismatch); });
  auto invalid_pair = configuration();
  invalid_pair.qubit_pairs = {{0, 0}};
  faults(ErrorCode::InvalidProfile,
         [&] { EqasmCpuCycleModel cpu(profile, 0, trace, invalid_pair); });
  auto too_many = profile;
  too_many.qubits = 8;
  faults(ErrorCode::InvalidProfile,
         [&] { EqasmCpuCycleModel cpu(too_many, 0, trace, configuration()); });
}
} // namespace
int main() {
  try {
    instructions();
    stalled_lane();
    invalid();
    reset();
    config();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
