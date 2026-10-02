#include "qsbit/defaults.hpp"
#include "qsbit/timing_control.hpp"
#include "test.hpp"

using namespace qsbit;

int main() {
  try {
    for (std::uint32_t mode = 0; mode < 4; ++mode) {
      const auto word = (mode << 25) | (9U << 20) | (3U << 15) | 0x0b;
      const auto decoded = rv32::decode(word);
      const auto operation = adapt_quantum(decoded, 1, 100, 200);
      CHECK(operation.kind == ControlKind::Codeword);
      CHECK(operation.first == (mode & 1 ? 3U : 100U));
      CHECK(operation.second == (mode & 2 ? 9U : 200U));
      CHECK(decoded.reads_rs1 == ((mode & 1) == 0));
      CHECK(decoded.reads_rs2 == ((mode & 2) == 0));
      CHECK(!decoded.writes_rd);
    }
    for (std::uint32_t interval : {0U, 1U, 131071U}) {
      const auto decoded = rv32::decode((interval << 15) | 0x200b);
      const auto operation = adapt_quantum(decoded, 1, 99, 99);
      CHECK(operation.kind == ControlKind::Wait && operation.first == interval);
      CHECK(!decoded.reads_rs1 && !decoded.reads_rs2 && !decoded.writes_rd);
    }
    const auto wait = adapt_quantum(rv32::decode((5U << 15) | 0x100b), 2, 0xffffffff, 0);
    CHECK(wait.kind == ControlKind::Wait && wait.first == 0xffffffff);
    const auto decoded = rv32::decode((31U << 15) | (10U << 7) | 0x300b);
    const auto read = adapt_quantum(decoded, 3, 123, 456);
    CHECK(read.kind == ControlKind::FetchMeasurement && read.first == 31);
    CHECK(decoded.writes_rd && decoded.rd == 10 && !decoded.reads_rs1);
    for (auto word :
         {0x400bU, 0x500bU, 0x700bU, 0x0200100bU, 0x0200300bU, 0x0800000bU, 0x0000008bU})
      faults(ErrorCode::IllegalInstruction, [&] { (void)rv32::decode(word); });
    auto p = default_profile();
    MeasurementRegisters registers(p);
    Trace trace;
    ControlLinks links(p);
    TimingControl control(p, registers, trace, [](const EventSpec &) {});
    CHECK(control.execute({1, ControlKind::Wait, 8}, 0, 1, links) == 0);
    CHECK(control.time_point() == 8);
    const auto sync = adapt_quantum(rv32::decode((17U << 15) | 0x600b), 2, 0, 0);
    CHECK(sync.kind == ControlKind::Synchronize && sync.first == 17);
    faults(ErrorCode::UnsupportedSynchronization,
           [&] { (void)control.execute(sync, 5, 1, links); });
    std::cout << "PASS control instructions\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
