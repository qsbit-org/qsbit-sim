#include "qsbit/decoder.hpp"
#include "test.hpp"

using namespace qsbit;
int main() {
  try {
    Trace trace;
    unsigned calls = 0;
    DecoderSystemConfig config;
    config.request_capacity = 1;
    config.result_capacity = 1;
    config.link_latency = 3;
    config.packet_overhead = 0;
    config.bytes_per_tick = 1;
    config.decoders.push_back({7, 2, 1, 10, 20, [&](std::span<const std::uint8_t> bits) {
                                 ++calls;
                                 return std::vector<bool>{bits[0] != bits[1]};
                               }});
    DecoderSystem device(config, trace);
    const auto write = [&](std::uint32_t core, unsigned offset, std::uint32_t value, Tick now) {
      return device.access(core, {1, config.base + offset, value, 4, true, false}, now, 1);
    };
    const auto read = [&](std::uint32_t core, unsigned offset, Tick now) {
      return device.access(core, {1, config.base + offset, 0, 4, false, false}, now, 1);
    };
    CHECK(device.contains(config.base) && !device.contains(config.base - 1));
    for (auto core : {0U, 1U}) {
      CHECK(write(core, 0, 7, 0));
      CHECK(write(core, 4, 2, 0));
      CHECK(write(core, 8, 1, 0));
    }
    CHECK(write(0, 20, 1, 0));
    CHECK(!write(1, 20, 1, 0));
    CHECK(!device.idle());
    faults(ErrorCode::Protocol, [&] { (void)read(0, 24, 0); });
    device.step(4, 1);
    CHECK(calls == 1);
    CHECK(write(1, 20, 1, 4));
    device.step(8, 1);
    CHECK(calls == 1); // The first result reserves the sole response slot.
    device.step(15, 1);
    CHECK(read(0, 20, 15) == 2);
    device.step(19, 1);
    CHECK(read(0, 20, 19) == 1 && read(0, 24, 19) == 1);
    CHECK(read(1, 20, 19) == 2);
    CHECK(write(0, 20, 3, 19));
    device.step(20, 1);
    CHECK(calls == 2);
    device.step(25, 1);
    device.step(35, 1);
    device.step(39, 1);
    CHECK(read(1, 24, 39) == 1 && device.idle());
    CHECK(write(1, 20, 1, 40));
    device.step(44, 1);
    device.step(45, 1);
    device.step(55, 1);
    device.step(59, 1);
    CHECK(read(1, 24, 59) == 0); // Corrections accumulate modulo two until consumed.
    CHECK(write(1, 20, 3, 59));
    CHECK(write(1, 20, 1, 60));
    device.step(64, 1);
    CHECK(write(1, 20, 2, 64));
    device.step(68, 1);
    device.step(100, 1);
    CHECK(read(1, 20, 100) == 0 && device.idle());
    device.reset();
    CHECK(device.idle());
    CHECK(write(0, 0, 7, 140));
    CHECK(read(0, 20, 140) == 0);
    CHECK(write(0, 4, 1, 140));
    CHECK(write(0, 8, 0, 140));
    CHECK(write(0, 20, 1, 140));
    device.step(144, 2);
    CHECK(!device.idle()); // An incomplete measurement window cannot silently drain.
    device.reset();
    CHECK(device.idle());
    faults(ErrorCode::InvalidOperand, [&] { (void)write(0, 0, 99, 145); });
    faults(ErrorCode::InvalidOperand,
           [&] { (void)device.access(0, {1, config.base, 0, 1, false, false}, 145, 2); });
    faults(ErrorCode::InvalidProfile,
           [&] { device.validate_memory(ProgramImage(config.base - 4, 64)); });
    device.reset();
    config.decoders.front().measurements = 64;
    config.decoders.front().outputs = 64;
    config.decoders.front().decode = [](std::span<const std::uint8_t> bits) {
      return std::vector<bool>(bits.begin(), bits.end());
    };
    DecoderSystem wide(config, trace);
    const auto send = [&](unsigned offset, std::uint32_t value) {
      CHECK(wide.access(0, {1, config.base + offset, value, 4, true, false}, 200, 3));
    };
    send(0, 7);
    send(4, 64);
    send(8, 0x80000001);
    send(12, 0x80000002);
    send(20, 1);
    wide.step(212, 3);
    wide.step(213, 3);
    wide.step(223, 3);
    wide.step(234, 3);
    CHECK(wide.access(0, {1, config.base + 24}, 234, 3) == 0x80000001);
    CHECK(wide.access(0, {1, config.base + 28}, 234, 3) == 0x80000002);
    std::cout << "PASS decoder transport, capacity, routing and reset\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
