#include "qsbit/image.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/mailbox.hpp"
#include "qsbit/memory.hpp"
#include "test.hpp"
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <vector>

using namespace qsbit;
using namespace qsbit::rv32;

void isa_arithmetic() {
  struct Case {
    std::uint32_t word, a, b, result;
  };
  const std::array cases{Case{0xfff08093, 0, 0, 0xffffffff}, // addi x1,x1,-1
                         Case{0x002080b3, 0xffffffff, 1, 0}, // add x1,x1,x2
                         Case{0x402080b3, 0, 1, 0xffffffff},
                         Case{0x002090b3, 1, 63, 0x80000000},
                         Case{0x0020d0b3, 0x80000000, 31, 1},
                         Case{0x4020d0b3, 0x80000000, 31, 0xffffffff},
                         Case{0x0020a0b3, 0xffffffff, 1, 1},
                         Case{0x0020b0b3, 0xffffffff, 1, 0},
                         Case{0xfff0b093, 0xfffffffe, 0, 1},
                         Case{0xfff0a093, 0x80000000, 0, 1},
                         Case{0x0020c0b3, 0xaaaa, 0x5555, 0xffff},
                         Case{0x0020e0b3, 0xf0, 0x0f, 0xff},
                         Case{0x0020f0b3, 0xf0, 0x0f, 0},
                         Case{0xfff0c093, 0xaaaa5555, 0, 0x5555aaaa},
                         Case{0x0050e093, 0x10, 0, 0x15},
                         Case{0x00f0f093, 0xff, 0, 0x0f},
                         Case{0x01f09093, 1, 0, 0x80000000},
                         Case{0x01f0d093, 0x80000000, 0, 1},
                         Case{0x41f0d093, 0x80000000, 0, 0xffffffff},
                         Case{0xfffff0b7, 0, 0, 0xfffff000},
                         Case{0xfffff097, 0, 0, 0xfffff100}};
  for (const auto &c : cases) {
    const auto d = decode(c.word);
    const auto e = evaluate(d, 0x100, c.a, c.b);
    CHECK(e.value == c.result);
    CHECK(e.next_pc == 0x104);
    CHECK(e.writes_rd);
  }
  CHECK(sign_extend(0x80, 8) == 0xffffff80);
  CHECK(sign_extend(0x8000, 16) == 0xffff8000);
  CHECK(sign_extend(0x100, 8) == 0);
  CHECK(sign_extend(0xffffffff, 32) == 0xffffffff);
  CHECK(evaluate(decode(0x00108093), 0xfffffffc, 0xffffffff, 0).next_pc == 0);
}
void isa_control() {
  const auto jump = evaluate(decode(0x008000ef), 0x100, 0, 0);
  CHECK(jump.next_pc == 0x108 && jump.value == 0x104);
  CHECK(evaluate(decode(0x000080e7), 0x100, 0x201, 0).next_pc == 0x200);
  faults(ErrorCode::InstructionMisaligned, [] { (void)evaluate(decode(0x000080e7), 0, 3, 0); });
  struct Branch {
    std::uint32_t word, a, b;
    bool taken;
  };
  for (const auto c : std::array{
           Branch{0x00208463, 1, 1, true}, Branch{0x00209463, 1, 1, false},
           Branch{0x0020c463, 0xffffffff, 1, true}, Branch{0x0020d463, 0xffffffff, 1, false},
           Branch{0x0020e463, 0xffffffff, 1, false}, Branch{0x0020f463, 0xffffffff, 1, true}}) {
    const auto e = evaluate(decode(c.word), 0x100, c.a, c.b);
    CHECK(e.branch_taken == c.taken);
    CHECK(e.next_pc == (c.taken ? 0x108U : 0x104U));
  }
  // A not-taken branch with a misaligned target is legal.
  CHECK(evaluate(decode(0x00208163), 0, 1, 2).next_pc == 4);
  faults(ErrorCode::InstructionMisaligned, [] { (void)evaluate(decode(0x00208163), 0, 1, 1); });
  for (auto word : {0xffc08083U, 0xffc09083U, 0xffc0a083U, 0xffc0c083U, 0xffc0d083U}) {
    const auto e = evaluate(decode(word), 0, 0x100, 0);
    CHECK(e.memory == MemoryKind::Load && e.address == 0xfc && e.writes_rd);
  }
  for (auto word : {0xfe208e23U, 0xfe209e23U, 0xfe20ae23U}) {
    const auto e = evaluate(decode(word), 0, 0x100, 0x12345678);
    CHECK(e.memory == MemoryKind::Store && e.address == 0xfc && e.store_value == 0x12345678);
  }
  faults(ErrorCode::EnvironmentCall, [] { (void)evaluate(decode(0x73), 0, 0, 0); });
  faults(ErrorCode::Breakpoint, [] { (void)evaluate(decode(0x100073), 0, 0, 0); });
  CHECK(!evaluate(decode(0x0ff0000f), 0, 0, 0).writes_rd);
}
void isa_decode() {
  for (auto word : {0U, 0xffffffffU, 0x022080b3U, 0x02009093U, 0x40009093U, 0x0000100fU,
                    0x30001073U, 0x0020a463U, 0x0000b083U, 0x0020b023U, 0x000010e7U, 0x0200000bU,
                    0x0000700bU, 0x0000108bU, 0x0000208bU, 0x0010300bU, 0x0000408bU})
    faults(ErrorCode::IllegalInstruction, [word] { (void)decode(word); });
  for (std::uint32_t f = 0; f < 7; ++f)
    CHECK(decode(0xb | (f << 12)).op == Op::Quantum);
  CHECK(decode(0x0200500b).op == Op::Quantum);
}
std::vector<std::uint8_t> elf_fixture() {
  std::vector<std::uint8_t> b(88);
  const auto put = [&](std::size_t p, std::uint32_t value, unsigned width = 4) {
    for (unsigned n = 0; n < width; ++n)
      b[p + n] = static_cast<std::uint8_t>(value >> (n * 8));
  };
  put(0, 0x464c457f);
  b[4] = 1;
  b[5] = 1;
  b[6] = 1;
  put(16, 2, 2);
  put(18, 243, 2);
  put(20, 1);
  put(24, 0x100);
  put(28, 52);
  put(40, 52, 2);
  put(42, 32, 2);
  put(44, 1, 2);
  put(52, 1);
  put(56, 84);
  put(60, 0x100);
  put(68, 4);
  put(72, 8);
  put(76, 5);
  put(80, 4);
  put(84, 0x00100093);
  return b;
}
void image_test() {
  const auto bytes = elf_fixture();
  auto image = ProgramImage::elf(bytes, 0, 0x1000);
  CHECK(image.entry() == 0x100);
  CHECK(image.read(0x100, 4, true) == 0x00100093);
  CHECK(image.read(0x104, 4) == 0);
  image.write(0x200, 4, 0xaabbccdd);
  CHECK(image.read(0x200, 1) == 0xdd && image.read(0x202, 2) == 0xaabb);
  faults(ErrorCode::StoreAccess, [&] { image.write(0x100, 4, 0); });
  faults(ErrorCode::InstructionAccess, [&] { (void)image.read(0x200, 4, true); });
  faults(ErrorCode::LoadMisaligned, [&] { (void)image.read(0x201, 4); });
  faults(ErrorCode::LoadAccess, [&] { (void)image.read(0x1000, 4); });
  for (const auto field : {4, 5, 18, 24, 28, 36, 40, 42, 44, 56, 68, 73, 80}) {
    auto bad = bytes;
    bad[static_cast<std::size_t>(field)] = 0xff;
    faults(ErrorCode::InvalidImage, [&] { (void)ProgramImage::elf(bad, 0, 0x1000); });
  }
  faults(ErrorCode::InvalidImage,
         [&] { (void)ProgramImage::elf(std::span(bytes).first(80), 0, 0x1000); });
  faults(ErrorCode::InvalidImage, [&] { (void)ProgramImage::raw(bytes, 1, 0, 0x1000); });
}
void mailbox_test() {
  const Clock c{20, 0};
  CHECK(c.after(19) == 20);
  CHECK(c.after(20) == 40);
  CHECK(c.after(20, 2) == 60);
  CHECK((Clock{20, 3}.after(0) == 3));
  CHECK((Clock{20, 3}.after(3) == 23));
  Mailbox<int> box(1, c);
  box.publish(20, 7, 42);
  CHECK(box.peek(20) == nullptr && box.peek(39) == nullptr && box.full());
  faults(ErrorCode::Capacity, [&] { box.publish(21, 7, 43); });
  const auto result = box.take(40);
  CHECK(result && result->epoch == 7 && result->value == 42);
  CHECK(!box.take(40));
  box.publish(40, 7, 43);
  box.reset();
  CHECK(box.empty());
  faults(ErrorCode::TimeOverflow, [] { (void)checked_add(std::numeric_limits<Tick>::max(), 1); });
  faults(ErrorCode::TimeOverflow, [] { (void)checked_mul(std::numeric_limits<Tick>::max(), 2); });
}
void memory_test() {
  const std::array<std::uint8_t, 4> code{0x13, 0, 0, 0};
  MemoryModel memory(ProgramImage::raw(code, 0, 0, 1024), Clock{5, 0}, 1);
  MemoryPort fetch(Clock{5, 0}), data(Clock{5, 0});
  data.requests.publish(0, 1, MemoryRequest{1, 0x100, 0x12345678, 4, true, false});
  memory.step(0, 1, fetch, data);
  CHECK(memory.image().read(0x100, 4) == 0);
  memory.step(5, 1, fetch, data);
  CHECK(memory.image().read(0x100, 4) == 0);
  memory.step(10, 1, fetch, data);
  CHECK(memory.image().read(0x100, 4) == 0x12345678);
  CHECK(!data.responses.take(10));
  CHECK(data.responses.take(15)->value.id == 1);
  data.requests.publish(15, 1, MemoryRequest{2, 0x100, 0, 4, false, false});
  memory.step(20, 1, fetch, data);
  memory.step(25, 1, fetch, data);
  CHECK(data.responses.take(30)->value.value == 0x12345678);
  data.requests.publish(30, 1, MemoryRequest{3, 0x100, 0, 4, true, false});
  memory.step(35, 1, fetch, data);
  memory.reset();
  data.reset();
  fetch.reset();
  memory.step(40, 2, fetch, data);
  CHECK(memory.image().read(0x100, 4) == 0x12345678);
  data.requests.publish(40, 2, MemoryRequest{1, 0x101, 0, 4, false, false});
  memory.step(45, 2, fetch, data);
  memory.step(50, 2, fetch, data);
  CHECK(data.responses.take(55)->value.fault == ErrorCode::LoadMisaligned);
}
int main(int argc, char **argv) {
  try {
    const std::map<std::string, std::function<void()>> tests{
        {"isa_arithmetic", isa_arithmetic}, {"isa_control", isa_control},
        {"isa_decode", isa_decode},         {"image", image_test},
        {"mailbox", mailbox_test},          {"memory", memory_test}};
    CHECK(argc == 2);
    tests.at(argv[1])();
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
