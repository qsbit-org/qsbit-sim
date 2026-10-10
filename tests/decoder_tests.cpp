#include "qsbit/decoder.hpp"
#include "test.hpp"
#include <algorithm>
#include <sstream>

using namespace qsbit;
namespace {
class Responses {
public:
  Responses(std::vector<Tick> latencies, std::uint32_t outputs = 1, Tick link_latency = 1)
      : Responses(configuration(latencies, outputs, link_latency)) {}
  explicit Responses(DecoderSystemConfig config)
      : config_(std::move(config)), device_(config_, trace_) {}

  void submit(std::uint32_t core) {
    write(core, 0, core);
    write(core, 4, 1);
    command(core, 1);
  }
  void command(std::uint32_t core, std::uint32_t value) { CHECK(try_command(core, value)); }
  bool try_command(std::uint32_t core, std::uint32_t value) {
    if (!device_.access(core, {1, config_.base + 20, value, 4, true, false}, now_, 1))
      return false;
    device_.step(now_, 1);
    return true;
  }
  void write(std::uint32_t core, unsigned offset, std::uint32_t value) {
    CHECK(device_.access(core, {1, config_.base + offset, value, 4, true, false}, now_, 1));
  }
  void advance(Tick target) {
    CHECK(target >= now_);
    while (auto next = device_.next_boundary(now_)) {
      if (*next > target)
        break;
      now_ = *next;
      device_.step(now_, 1);
    }
    if (now_ < target) {
      now_ = target;
      device_.step(now_, 1);
    }
  }
  std::uint32_t status(std::uint32_t core) {
    return device_.access(core, {1, config_.base + 20}, now_, 1).value();
  }
  void expect_returns(const std::vector<std::pair<std::uint32_t, Tick>> &expected) const {
    std::vector<std::pair<std::uint32_t, Tick>> returned;
    for (const auto &event : trace_.events())
      if (event.kind == "DecoderResultReturned")
        returned.emplace_back(event.core.value(), event.tick);
    CHECK(returned == expected);
  }
  void reset() { device_.reset(); }
  bool idle() const { return device_.idle(); }
  std::optional<Tick> next_boundary() const { return device_.next_boundary(now_); }

private:
  static DecoderSystemConfig configuration(const std::vector<Tick> &latencies,
                                           std::uint32_t outputs, Tick link_latency) {
    DecoderSystemConfig config;
    config.request_capacity = 4;
    config.result_capacity = 2;
    config.link_latency = link_latency;
    config.packet_overhead = 0;
    for (std::uint32_t id = 0; id < latencies.size(); ++id)
      config.decoders.push_back({id, 1, outputs, latencies[id], 1,
                                 [outputs](auto) { return std::vector<bool>(outputs, true); }});
    return config;
  }
  Trace trace_;
  DecoderSystemConfig config_;
  DecoderSystem device_;
  Tick now_ = 0;
};

void mmio_commands() {
  DecoderMmio mmio(0x40000000, {7});
  const auto write = [&](unsigned offset, std::uint32_t value) {
    return mmio.access(2, {1, 0x40000000 + offset, value, 4, true, false});
  };
  CHECK(!write(0, 7));
  CHECK(!write(4, 64));
  CHECK(!write(8, 0x89abcdef));
  CHECK(!write(12, 0x12345678));
  CHECK(!write(16, 19));
  const auto command = write(20, 1);
  const auto &submit = std::get<DecoderSubmit>(*command);
  CHECK(submit.decoder == 7 && submit.count == 64 && submit.tag == 19);
  CHECK(submit.data == 0x1234567889abcdefULL);
  const auto reset = write(20, 2);
  CHECK(std::get<DecoderReset>(*reset).decoder == 7);
  CHECK(std::get<DecoderRead>(*mmio.access(2, {1, 0x40000018})).kind == DecoderReadKind::Low);
  mmio.reset();
  faults(ErrorCode::InvalidOperand, [&] { (void)write(20, 1); });
}
void response_order() {
  Responses responses(std::vector<Tick>{100, 1});
  responses.submit(0);
  responses.advance(2);
  responses.submit(1);
  responses.advance(7);
  CHECK(responses.status(1) == 2);
  responses.advance(8);
  CHECK(responses.status(1) == 1 && responses.status(0) == 2);
  responses.advance(104);
  CHECK(responses.status(0) == 2);
  responses.advance(105);
  CHECK(responses.status(0) == 1);
  responses.expect_returns({{1, 8}, {0, 105}});
}

void response_contention() {
  // The first two jobs complete at 22; each response takes 8 ns plus 10 ns propagation.
  Responses responses({10, 9, 1}, 64, 10);
  responses.submit(0);
  responses.submit(1);
  responses.submit(2);
  responses.advance(39);
  CHECK(responses.status(0) == 2 && responses.status(1) == 2);
  responses.advance(40);
  CHECK(responses.status(0) == 1);
  responses.advance(47);
  CHECK(responses.status(1) == 2);
  responses.advance(48);
  CHECK(responses.status(1) == 1);
  responses.advance(60);
  CHECK(responses.status(2) == 2);
  responses.command(0, 3);
  responses.advance(79);
  CHECK(responses.status(2) == 2);
  responses.advance(80);
  CHECK(responses.status(2) == 1);
  responses.expect_returns({{0, 40}, {1, 48}, {2, 80}});
}

void response_cancellation() {
  for (auto cancelled : {0U, 1U}) {
    // At 13, core 0 starts transmitting while core 1 waits. Reset arrives at 15.
    Responses responses({10, 9}, 64);
    responses.submit(0);
    responses.submit(1);
    responses.advance(13);
    responses.command(cancelled, 2);
    responses.advance(15);
    CHECK(responses.status(cancelled) == 0);
    responses.submit(cancelled);
    const Tick returned = cancelled == 0 ? 38 : 36;
    responses.advance(returned - 1);
    CHECK(responses.status(cancelled) == 2);
    responses.advance(returned);
    CHECK(responses.status(cancelled) == 1);
    responses.expect_returns({{1 - cancelled, cancelled == 0 ? 30 : 22}, {cancelled, returned}});
  }
  Responses responses({1, 1}, 64);
  responses.submit(0);
  responses.advance(5);
  responses.reset();
  responses.submit(1);
  responses.advance(17);
  CHECK(responses.status(1) == 2);
  responses.advance(18);
  CHECK(responses.status(1) == 1 && responses.status(0) == 0);
  responses.expect_returns({{1, 18}});
}

void reset_full_queue() {
  DecoderSystemConfig config;
  config.request_capacity = config.result_capacity = 1;
  config.link_latency = 3;
  config.packet_overhead = 2;
  for (auto id : {0U, 1U})
    config.decoders.push_back({id, 1, 1, 2, 1, [](auto) { return std::vector<bool>{true}; }});
  Responses responses(config);
  responses.submit(0);
  responses.advance(15);
  CHECK(responses.status(0) == 1);
  responses.write(0, 0, 1);
  responses.command(0, 1);
  responses.advance(21);
  CHECK(responses.status(0) == 2);
  CHECK(!responses.try_command(0, 1));
  responses.write(0, 0, 0);
  responses.command(0, 2);
  CHECK(responses.status(0) == 3);
  CHECK(!responses.try_command(1, 2));
  CHECK(responses.status(1) == 0);
  CHECK(responses.next_boundary() == 26);
  responses.advance(25);
  CHECK(responses.status(0) == 3);
  responses.advance(26);
  CHECK(responses.status(0) == 0);
  responses.write(0, 0, 1);
  responses.advance(34);
  CHECK(responses.status(0) == 2);
  responses.advance(35);
  CHECK(responses.status(0) == 1);
  responses.expect_returns({{0, 15}, {0, 35}});
  responses.command(0, 2);
  responses.advance(40);
  CHECK(responses.status(0) == 0 && responses.idle());
}

void reset_request_order() {
  DecoderSystemConfig config;
  config.request_capacity = 5;
  config.result_capacity = 1;
  config.packet_overhead = 0;
  config.decoders.push_back({0, 2, 1, 20, 1, [](auto) { return std::vector<bool>{true}; }});
  Responses responses(config);
  responses.write(1, 4, 1);
  responses.command(1, 1);
  responses.write(0, 4, 2);
  responses.command(0, 1);
  responses.command(0, 1);
  responses.command(0, 2);
  responses.command(0, 1);
  responses.command(1, 1);
  responses.advance(28);
  CHECK(responses.status(0) == 2 && responses.status(1) == 2);
  responses.advance(29);
  CHECK(responses.status(0) == 1);
  responses.command(0, 3);
  responses.advance(51);
  CHECK(responses.status(1) == 2);
  responses.advance(52);
  CHECK(responses.status(1) == 1 && responses.idle());
  responses.expect_returns({{0, 29}, {1, 52}});

  Responses partial(config);
  partial.write(0, 4, 1);
  partial.command(0, 1);
  partial.advance(2);
  partial.command(0, 2);
  partial.write(0, 4, 2);
  partial.command(0, 1);
  partial.advance(28);
  CHECK(partial.status(0) == 1 && partial.idle());
  partial.expect_returns({{0, 28}});
}

void reset_boundaries() {
  Responses responses(std::vector<Tick>{1});
  responses.submit(0);
  responses.advance(4);
  responses.command(0, 2);
  responses.advance(6);
  CHECK(responses.status(0) == 0);
  responses.expect_returns({});
  responses.submit(0);
  responses.advance(12);
  CHECK(responses.status(0) == 1);
  responses.expect_returns({{0, 12}});
  responses.command(0, 2);
  CHECK(responses.status(0) == 3 && !responses.idle());
  CHECK(responses.next_boundary() == 14);
  responses.reset();
  CHECK(responses.status(0) == 0 && responses.idle() && !responses.next_boundary());
  responses.advance(20);
  responses.command(0, 2);
  CHECK(responses.status(0) == 2 && !responses.idle());
  CHECK(responses.next_boundary() == 22);
  responses.advance(22);
  CHECK(responses.status(0) == 0 && responses.idle());
}
} // namespace
int main() {
  try {
    mmio_commands();
    response_order();
    response_contention();
    response_cancellation();
    reset_full_queue();
    reset_request_order();
    reset_boundaries();
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
    const auto returned =
        std::find_if(trace.events().begin(), trace.events().end(),
                     [](const auto &event) { return event.kind == "DecoderResultReturned"; });
    CHECK(returned != trace.events().end() && returned->decoder);
    CHECK(returned->decoder->id == 7 && returned->decoder->jobs == 1);
    std::ostringstream serialized;
    trace.write_jsonl(serialized);
    CHECK(serialized.str().find("\"decoder\":{\"id\":7") != std::string::npos);
    std::cout << "PASS decoder transport, capacity, routing and reset\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
