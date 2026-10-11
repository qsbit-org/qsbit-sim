#include "qsbit/control_links.hpp"
#include "qsbit/defaults.hpp"
#include "qsbit/device.hpp"
#include "qsbit/image.hpp"
#include "qsbit/profile.hpp"
#include "qsbit/timing_config.hpp"
#include "qsbit/timing_control.hpp"
#include "test.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <map>

using namespace qsbit;
TriggeredEvents launch(const Profile &p, std::uint32_t port, std::uint32_t code, Tick now,
                       Id id = 1, std::optional<MeasurementReference> reference = {}) {
  auto events = decode_codeword(timing_config(p), port, code, 1, id, id,
                                reference ? std::span{&*reference, 1}
                                          : std::span<const MeasurementReference>{});
  for (auto &event : events)
    event.label = id;
  return {1, id, now, std::move(events)};
}
void timing_test() {
  auto p = default_profile();
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  Trace trace;
  ControlLinks links(transport_config(p));
  TimingControl control(timing_config(p), registers, trace, [](const EventSpec &) {});
  CHECK(control.execute({1, WaitCommand{8}}, 0, 1, links) == 0);
  CHECK(control.execute({2, CodewordCommand{0, 1}}, 5, 1, links) == 0);
  CHECK(control.execute({3, CodewordCommand{1, 1}}, 10, 1, links) == 0);
  const ControlOperation advance{4, WaitCommand{3}};
  CHECK(!control.execute(advance, 15, 1, links));
  CHECK(control.pending() && control.time_point().value == 8);
  CHECK(!control.execute(advance, 20, 1, links));
  faults(ErrorCode::Protocol, [&] { (void)control.execute({5, HaltCommand{}}, 20, 1, links); });
  auto group = links.timing_events.take(20);
  CHECK(group && group->value.events.size() == 2 && group->value.point.interval.value == 8);
  CHECK(group->value.point.manifest[0] != group->value.point.manifest[1]);
  links.replies.publish(20, 1, {1});
  control.receive(20, 1, links);
  CHECK(control.pending());
  control.receive(25, 1, links);
  CHECK(control.execute(advance, 25, 1, links) == 0 && control.time_point().value == 11);
  CHECK(!control.execute({6, WaitCommand{0}}, 30, 1, links));
  CHECK(control.time_point().value == 11);
  auto final = links.timing_events.take(40);
  CHECK(final && final->value.events.empty() && final->value.point.interval.value == 3);
  CHECK(final->value.point.underflow_policy == UnderflowPolicy::PauseWhenEmpty);
  links.replies.publish(40, 1, {2});
  control.receive(45, 1, links);
  CHECK(control.execute({6, WaitCommand{0}}, 45, 1, links) == 0);
  CHECK(control.execute({7, HaltCommand{}}, 50, 1, links) == 0);
  CHECK(control.closed() && links.closure.take(60)->value.last_label == 2);
  faults(ErrorCode::Protocol, [&] { (void)control.execute({8, WaitCommand{1}}, 55, 1, links); });
}
void capacity_test() {
  auto p = default_profile();
  p.result_capacity = 1;
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  Trace trace;
  ControlLinks links(transport_config(p));
  TimingControl control(timing_config(p), registers, trace, [](const EventSpec &) {});
  CHECK(control.execute({1, CodewordCommand{0, 4}}, 0, 1, links) == 0);
  faults(ErrorCode::Capacity,
         [&] { (void)control.execute({2, CodewordCommand{1, 4}}, 5, 1, links); });
  CHECK(trace.events().size() == 1);
  control.reset();
  registers.reset();
  CHECK(control.execute({1, CodewordCommand{0, 1}}, 10, 2, links) == 0);
  faults(ErrorCode::Capacity,
         [&] { (void)control.execute({2, CodewordCommand{0, 2}}, 15, 2, links); });
  CHECK(trace.events().size() == 2);
}
void resources_test() {
  auto p = default_profile();
  MockBackend backend;
  Trace trace;
  ControlElectronics device(device_profile(p), backend, trace);
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
  DiscriminatorArmSpec specification;
  specification.operands = arm.get<AcquireSpec>().operands;
  specification.operation = arm.get<AcquireSpec>().operation;
  arm.spec = specification;
  arm.port = 2;
  arm.get<DiscriminatorArmSpec>().operands.resources = {{20, true}};
  arm.delay = 80;
  arm.duration = 1;
  map.actions.push_back(arm);
  p.validate();
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  const auto reference = registers.reserve(1, 0);
  MockBackend backend({{reference.measurement, true}});
  Trace trace;
  ControlLinks links(transport_config(p));
  ControlElectronics device(device_profile(p), backend, trace);
  auto batch = launch(p, 0, 4, 100, 1, reference);
  auto corrupted = batch;
  ++corrupted.events.back().reference->epoch;
  faults(ErrorCode::InvalidMeasurement, [&] { device.accept(corrupted); });
  CHECK(device.drained() && device.resources().reservations().empty());
  device.accept(batch);
  faults(ErrorCode::Protocol, [&] { device.finalize(100); });
  for (Tick tick : {100U, 140U, 180U, 181U})
    device.process(tick, 1, [&](const Completion &result) {
      links.cpu_results.publish(tick, 1, result);
      if (p.fast_feedback)
        links.fast_results.publish(tick, 1, result);
    });
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
  p.mappings[3].actions.back().get<DiscriminatorArmSpec>().operands.targets = {1};
  faults(ErrorCode::InvalidProfile, [&] { p.validate(); });
}
void overflow_test() {
  auto p = default_profile();
  p.mappings[3].actions[0].duration = 1;
  p.mappings[3].actions[0].get<AcquireSpec>().discriminator_delay = 10;
  MockBackend backend;
  Trace trace;
  ControlElectronics device(device_profile(p), backend, trace);
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
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
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  MockBackend backend;
  Trace trace;
  ControlLinks links(transport_config(p));
  ControlElectronics device(device_profile(p), backend, trace);
  device.accept(launch(p, 0, 4, 100, 1, registers.reserve(1, 0)));
  device.process(100, 1, [&](const Completion &result) {
    links.cpu_results.publish(100, 1, result);
    if (p.fast_feedback)
      links.fast_results.publish(100, 1, result);
  });
  device.reset(140, 2);
  CHECK(device.drained() && links.cpu_results.empty());
  CHECK(trace.events().back().kind == "ResetAborted");
  device.process(140, 2, [&](const Completion &result) {
    links.cpu_results.publish(140, 2, result);
    if (p.fast_feedback)
      links.fast_results.publish(140, 2, result);
  });
  CHECK(links.cpu_results.empty());
}
void sample_collision_test() {
  auto p = default_profile();
  MockBackend backend;
  Trace trace;
  ControlElectronics device(device_profile(p), backend, trace);
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  device.accept(launch(p, 0, 4, 100, 1, registers.reserve(1, 0)));
  faults(ErrorCode::ResourceConflict, [&] { device.accept(launch(p, 0, 1, 140, 2)); });
  CHECK(device.resources().reservations().size() == 1);
}
void flags_test() {
  auto p = default_profile();
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  ExecutionFlags flags(p.qubits);
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
void batch_readout_test() {
  auto p = default_profile();
  p.result_capacity = 2;
  auto first = p.mapping(0, 4).actions.front();
  auto second = p.mapping(1, 4).actions.front();
  second.duration = 60;
  first.get<AcquireSpec>().separate_arm = true;
  EventSpec arm = first;
  arm.spec = DiscriminatorArmSpec{"measure", first.get<AcquireSpec>().operands};
  arm.port = 2;
  arm.duration = 1;
  arm.get<DiscriminatorArmSpec>().operands.resources = {{20, true}};
  p.mappings.push_back({0, 50, {arm, second, first}});
  p.validate();
  auto invalid = p;
  invalid.mappings.back().actions.push_back(first);
  faults(ErrorCode::InvalidProfile, [&] { invalid.validate(); });
  invalid = p;
  invalid.mappings.back().actions.erase(invalid.mappings.back().actions.begin());
  faults(ErrorCode::InvalidProfile, [&] { invalid.validate(); });

  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  const std::array<std::uint32_t, 2> targets{1, 0};
  const auto refs = registers.prepare_reservation(1, targets);
  CHECK(refs[0].measurement == 1 && refs[1].measurement == 2 && !registers.deliveries_pending());
  auto events = decode_codeword(timing_config(p), 0, 50, 1, 1, 1, refs);
  for (auto &event : events)
    event.label = 1;
  CHECK(events[0].reference == refs[1] && events[1].reference == refs[0]);
  auto bad = refs;
  bad[0].target = 0;
  faults(ErrorCode::InvalidMeasurement,
         [&] { (void)decode_codeword(timing_config(p), 0, 50, 1, 1, 1, bad); });
  faults(ErrorCode::TimeOverflow, [&] {
    (void)decode_codeword(timing_config(p), 0, 50, 1, 1, std::numeric_limits<Id>::max(), refs);
  });
  CHECK(!registers.deliveries_pending());
  registers.commit_reservation(refs);
  CHECK(!registers.has_capacity() && !registers.read(0) && !registers.read(1));
  faults(ErrorCode::Capacity, [&] { (void)registers.prepare_reservation(1, targets); });
  MockBackend backend({{1, false}, {2, true}});
  Trace trace;
  ControlElectronics device(device_profile(p), backend, trace);
  device.accept({1, 1, 100, events});
  std::vector<Tick> ready;
  while (const auto tick = device.next_boundary())
    device.process(*tick, 1, [&](const Completion &completion) {
      ready.push_back(*tick);
      registers.deliver(completion, 1);
    });
  CHECK(ready == std::vector<Tick>({160, 180}));
  CHECK(registers.read(0) == true && registers.read(1) == false);
  CHECK(!registers.has_capacity()); // Fast acknowledgments independently retain credits.
  for (const auto &ref : refs)
    registers.acknowledge_fast(ref, 1);
  CHECK(registers.has_capacity(2));
  std::vector<Tick> starts;
  for (const auto &event : trace.events())
    if (event.kind == "OperationStart" && event.operation == "measure")
      starts.push_back(event.tick);
  CHECK(starts.size() >= 2 &&
        std::all_of(starts.begin(), starts.end(), [](auto t) { return t == 100; }));
  registers.reset();
  registers.deliver({refs.front(), true}, 2);
  CHECK(registers.read(1) == false);
}
void batch_atomic_test() {
  auto p = default_profile();
  p.result_capacity = 2;
  p.mappings.push_back({0, 50, {p.mapping(0, 4).actions.front(), p.mapping(1, 4).actions.front()}});
  p.validate();
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  // Staging rejection and backend rejection must also leave the entire group unreserved.
  {
    auto limited = p;
    limited.staging_capacity = 1;
    MeasurementRegisters empty({p.qubits, p.result_capacity, p.fast_feedback});
    Trace rejected;
    ControlLinks wires(transport_config(limited));
    TimingControl staging(timing_config(limited), empty, rejected, [](const EventSpec &) {});
    faults(ErrorCode::Capacity,
           [&] { (void)staging.execute({1, CodewordCommand{0, 50}}, 0, 1, wires); });
    CHECK(!empty.deliveries_pending() && rejected.events().empty());
    TimingControl unsupported(timing_config(p), empty, rejected, [](const EventSpec &action) {
      require(action.targets().front() == 0, ErrorCode::UnsupportedCapability,
              "unsupported target");
    });
    faults(ErrorCode::UnsupportedCapability,
           [&] { (void)unsupported.execute({1, CodewordCommand{0, 50}}, 0, 1, wires); });
    CHECK(!empty.deliveries_pending() && rejected.events().empty());
    CHECK(empty.reserve(1, 0).measurement == 1);
  }
  const auto occupied = registers.reserve(1, 0);
  Trace trace;
  ControlLinks links(transport_config(p));
  TimingControl control(timing_config(p), registers, trace, [](const EventSpec &) {});
  const ControlOperation command{1, CodewordCommand{0, 50}};
  faults(ErrorCode::Capacity, [&] { (void)control.execute(command, 0, 1, links); });
  CHECK(registers.registers()[0].pending == 1 && registers.registers()[1].pending == 0);
  CHECK(trace.events().empty());
  registers.deliver({occupied, false}, 1);
  faults(ErrorCode::Capacity, [&] { (void)control.execute(command, 5, 1, links); });
  registers.acknowledge_fast(occupied, 1);
  CHECK(control.execute(command, 10, 1, links) == 0);
  CHECK(registers.registers()[0].pending == 1 && registers.registers()[1].pending == 1);
  CHECK(!control.execute({2, WaitCommand{1}}, 15, 1, links));
  const auto request = links.timing_events.take(100);
  CHECK(request && request->value.events.size() == 2);
  CHECK(request->value.events[0].reference->measurement == 2);
  CHECK(request->value.events[1].reference->measurement == 3);
}
int main(int argc, char **argv) {
  try {
    const std::map<std::string, std::function<void()>> tests{
        {"batch_readout", batch_readout_test},
        {"batch_atomic", batch_atomic_test},
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
