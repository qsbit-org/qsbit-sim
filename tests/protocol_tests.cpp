#include "qsbit/defaults.hpp"
#include "qsbit/device.hpp"
#include "qsbit/image.hpp"
#include "test.hpp"
#include <functional>
#include <limits>
#include <map>

using namespace qsbit;
TriggeredEvents launch(const Profile &p, std::uint32_t port, std::uint32_t code, Tick now,
                       Id id = 1, std::optional<MeasurementReference> reference = {}) {
  auto events = decode_codeword(p, port, code, 1, id, id, reference);
  for (auto &event : events)
    event.label = id;
  return {1, id, now, std::move(events)};
}
void timing_test() {
  auto p = default_profile();
  MeasurementRegisters registers(p);
  Trace trace;
  ControlLinks links(p);
  TimingControl control(p, registers, trace, [](const EventSpec &) {});
  CHECK(control.execute({1, ControlKind::Wait, 8}, 0, 1, links) == 0);
  CHECK(control.execute({2, ControlKind::Codeword, 0, 1}, 5, 1, links) == 0);
  CHECK(control.execute({3, ControlKind::Codeword, 1, 1}, 10, 1, links) == 0);
  const ControlOperation advance{4, ControlKind::Wait, 3};
  CHECK(!control.execute(advance, 15, 1, links));
  CHECK(control.pending() && control.time_point() == 8);
  CHECK(!control.execute(advance, 20, 1, links));
  faults(ErrorCode::Protocol, [&] { (void)control.execute({5, ControlKind::Halt}, 20, 1, links); });
  auto group = links.timing_events.take(20);
  CHECK(group && group->value.events.size() == 2 && group->value.point.interval == 8);
  CHECK(group->value.point.manifest[0] != group->value.point.manifest[1]);
  links.replies.publish(20, 1, {1});
  control.receive(20, 1, links);
  CHECK(control.pending());
  control.receive(25, 1, links);
  CHECK(control.execute(advance, 25, 1, links) == 0 && control.time_point() == 11);
  CHECK(!control.execute({6, ControlKind::Wait, 0}, 30, 1, links));
  CHECK(control.time_point() == 11);
  auto final = links.timing_events.take(40);
  CHECK(final && final->value.events.empty() && final->value.point.interval == 3);
  CHECK(final->value.point.underflow_policy == UnderflowPolicy::PauseWhenEmpty);
  links.replies.publish(40, 1, {2});
  control.receive(45, 1, links);
  CHECK(control.execute({6, ControlKind::Wait, 0}, 45, 1, links) == 0);
  CHECK(control.execute({7, ControlKind::Halt}, 50, 1, links) == 0);
  CHECK(control.closed() && links.closure.take(60)->value.last_label == 2);
  faults(ErrorCode::Protocol,
         [&] { (void)control.execute({8, ControlKind::Wait, 1}, 55, 1, links); });
}
void capacity_test() {
  auto p = default_profile();
  p.result_capacity = 1;
  MeasurementRegisters registers(p);
  Trace trace;
  ControlLinks links(p);
  TimingControl control(p, registers, trace, [](const EventSpec &) {});
  CHECK(control.execute({1, ControlKind::Codeword, 0, 4}, 0, 1, links) == 0);
  faults(ErrorCode::Capacity,
         [&] { (void)control.execute({2, ControlKind::Codeword, 1, 4}, 5, 1, links); });
  CHECK(trace.events().size() == 1);
  control.reset();
  registers.reset();
  CHECK(control.execute({1, ControlKind::Codeword, 0, 1}, 10, 2, links) == 0);
  faults(ErrorCode::Capacity,
         [&] { (void)control.execute({2, ControlKind::Codeword, 0, 2}, 15, 2, links); });
  CHECK(trace.events().size() == 2);
}
void resources_test() {
  auto p = default_profile();
  MockBackend backend;
  Trace trace;
  ControlElectronics device(p, backend, trace);
  auto first = launch(p, 0, 1, 100);
  device.accept(first);
  const auto calls = backend.calls();
  auto conflict = launch(p, 0, 2, 110, 2);
  auto independent = launch(p, 1, 1, 110, 3);
  conflict.events.push_back(independent.events.front());
  conflict.events.back().label = conflict.label;
  faults(ErrorCode::ResourceConflict, [&] { device.accept(conflict); });
  CHECK(device.resources().reservations().size() == 1 && backend.calls() == calls);
  CHECK(trace.events().size() == 1);
  device.accept(launch(p, 0, 1, 120, 4));
  CHECK(device.resources().reservations().size() == 2);
}
void readout_test() {
  auto p = default_profile();
  auto &map = p.mappings[3];
  CHECK(map.actions[0].kind() == ActionKind::Acquire);
  map.actions[0].get<AcquireSpec>().separate_arm = true;
  map.actions[0].get<AcquireSpec>().discriminator_delay = 0;
  EventSpec arm = map.actions[0];
  ArmSpec specification;
  static_cast<QuantumSpec &>(specification) = arm.get<AcquireSpec>();
  arm.spec = specification;
  arm.port = 2;
  arm.get<ArmSpec>().resources = {{20, true}};
  arm.delay = 80;
  arm.duration = 1;
  map.actions.push_back(arm);
  p.validate();
  MeasurementRegisters registers(p);
  const auto reference = registers.reserve(1, 0);
  MockBackend backend({{reference.measurement, true}});
  Trace trace;
  ControlLinks links(p);
  ControlElectronics device(p, backend, trace);
  auto batch = launch(p, 0, 4, 100, 1, reference);
  auto corrupted = batch;
  ++corrupted.events.back().reference->epoch;
  faults(ErrorCode::InvalidMeasurement, [&] { device.accept(corrupted); });
  CHECK(device.drained() && device.resources().reservations().empty());
  device.accept(batch);
  faults(ErrorCode::Protocol, [&] { device.finalize(100); });
  for (Tick tick : {100U, 140U, 180U, 181U})
    device.process(tick, 1, links);
  CHECK(!links.cpu_results.take(180));
  const auto cpu = links.cpu_results.take(185);
  CHECK(cpu && cpu->value.value && cpu->value.reference == reference);
  CHECK(!links.fast_results.take(200));
  CHECK(links.fast_results.take(220)->value.reference == reference);
  CHECK(device.drained());
  faults(ErrorCode::Protocol, [&] { device.finalize(180); });
  device.finalize(220);
  std::vector<Tick> times;
  for (const auto &e : trace.events())
    if (e.kind == "MeasurementSampled" || e.kind == "ResultReady")
      times.push_back(e.tick);
  CHECK(times == std::vector<Tick>({140, 180}));
  p.mappings[3].actions.back().get<ArmSpec>().targets = {1};
  faults(ErrorCode::InvalidProfile, [&] { p.validate(); });
}
void overflow_test() {
  auto p = default_profile();
  p.mappings[3].actions[0].duration = 1;
  p.mappings[3].actions[0].get<AcquireSpec>().discriminator_delay = 10;
  MockBackend backend;
  Trace trace;
  ControlElectronics device(p, backend, trace);
  MeasurementRegisters registers(p);
  const auto reference = registers.reserve(1, 0);
  const auto calls = backend.calls();
  faults(ErrorCode::TimeOverflow, [&] {
    device.accept(launch(p, 0, 4, std::numeric_limits<Tick>::max() - 5, 1, reference));
  });
  CHECK(device.drained() && device.resources().reservations().empty());
  CHECK(trace.events().empty() && backend.calls() == calls);
  faults(ErrorCode::TimeOverflow,
         [] { (void)Clock{1, 0}.after(std::numeric_limits<Tick>::max()); });
  faults(ErrorCode::InvalidImage, [] { ProgramImage image(0xffffffffU, 0xffffffffU); });
}
void reset_test() {
  auto p = default_profile();
  MeasurementRegisters registers(p);
  MockBackend backend;
  Trace trace;
  ControlLinks links(p);
  ControlElectronics device(p, backend, trace);
  device.accept(launch(p, 0, 4, 100, 1, registers.reserve(1, 0)));
  device.process(100, 1, links);
  device.reset(140, 2);
  CHECK(device.drained() && links.cpu_results.empty());
  CHECK(trace.events().back().kind == "ResetAborted");
  device.process(140, 2, links);
  CHECK(links.cpu_results.empty());
}
void sample_collision_test() {
  auto p = default_profile();
  MockBackend backend;
  Trace trace;
  ControlElectronics device(p, backend, trace);
  MeasurementRegisters registers(p);
  device.accept(launch(p, 0, 4, 100, 1, registers.reserve(1, 0)));
  faults(ErrorCode::ResourceConflict, [&] { device.accept(launch(p, 0, 1, 140, 2)); });
  CHECK(device.resources().reservations().size() == 1);
}
void flags_test() {
  auto p = default_profile();
  MeasurementRegisters registers(p);
  ExecutionFlags flags(p);
  CHECK(flags.evaluate(0, ExecutionFlag::Always));
  CHECK(!flags.evaluate(0, ExecutionFlag::LastOne));
  CHECK(!flags.evaluate(0, ExecutionFlag::LastZero));
  CHECK(!flags.evaluate(0, ExecutionFlag::Equal));
  const auto first = registers.reserve(1, 0);
  const auto other = registers.reserve(1, 1);
  const auto last = registers.reserve(1, 0);
  flags.commit({other, true}, 1);
  flags.commit({first, false}, 1);
  CHECK(flags.evaluate(1, ExecutionFlag::LastOne));
  CHECK(flags.evaluate(0, ExecutionFlag::LastZero));
  CHECK(!flags.evaluate(0, ExecutionFlag::Equal));
  flags.commit({last, true}, 1);
  CHECK(flags.evaluate(0, ExecutionFlag::LastOne));
  CHECK(!flags.evaluate(0, ExecutionFlag::LastZero));
  CHECK(!flags.evaluate(0, ExecutionFlag::Equal));
  const auto repeated = registers.reserve(1, 0);
  flags.commit({repeated, true}, 1);
  CHECK(flags.evaluate(0, ExecutionFlag::Equal));
  faults(ErrorCode::Protocol, [&] { flags.commit({repeated, true}, 1); });
  flags.reset();
  flags.commit({last, true}, 2);
  CHECK(flags.evaluate(0, ExecutionFlag::Always));
  CHECK(!flags.evaluate(0, ExecutionFlag::LastOne));
}
int main(int argc, char **argv) {
  try {
    const std::map<std::string, std::function<void()>> tests{
        {"timing", timing_test},
        {"capacity", capacity_test},
        {"resources", resources_test},
        {"readout", readout_test},
        {"overflow", overflow_test},
        {"reset", reset_test},
        {"sample_collision", sample_collision_test},
        {"flags", flags_test}};
    CHECK(argc == 2);
    tests.at(argv[1])();
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
