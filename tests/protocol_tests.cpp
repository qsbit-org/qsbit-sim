#include "qsbit/defaults.hpp"
#include "qsbit/device.hpp"
#include "qsbit/image.hpp"
#include "test.hpp"
#include <functional>
#include <limits>
#include <map>

using namespace qsbit;
LaunchBatch launch(const Profile &p, std::uint32_t port, std::uint32_t code, Tick now, Id id = 1,
                   std::optional<Token> token = {}) {
  auto events = lower(p, port, code, 1, id, id, token);
  for (auto &event : events)
    event.label = id;
  return {1, id, now, std::move(events)};
}
void producer_test() {
  auto p = default_profile();
  Scoreboard slots(p);
  Trace trace;
  ControlLinks links(p);
  TimelineProducer producer(p, slots, trace, [](const ActionSpec &) {});
  CHECK(producer.execute({1, ProducerKind::Advance, 8}, 0, 1, links) == 0);
  CHECK(producer.execute({2, ProducerKind::Append, 0, 1}, 5, 1, links) == 0);
  CHECK(producer.execute({3, ProducerKind::Append, 1, 1}, 10, 1, links) == 0);
  const ProducerOperation advance{4, ProducerKind::Advance, 3};
  CHECK(!producer.execute(advance, 15, 1, links));
  CHECK(producer.pending() && producer.cursor() == 8);
  CHECK(!producer.execute(advance, 20, 1, links));
  faults(ErrorCode::Protocol,
         [&] { (void)producer.execute({5, ProducerKind::End}, 20, 1, links); });
  auto group = links.groups.take(20);
  CHECK(group && group->value.events.size() == 2 && group->value.point.interval == 8);
  CHECK(group->value.point.manifest[0] != group->value.point.manifest[1]);
  links.replies.publish(20, 1, {1});
  producer.receive(20, 1, links);
  CHECK(producer.pending());
  producer.receive(25, 1, links);
  CHECK(producer.execute(advance, 25, 1, links) == 0 && producer.cursor() == 11);
  CHECK(producer.execute({6, ProducerKind::Advance, 0}, 30, 1, links) == 0);
  CHECK(producer.cursor() == 11);
  CHECK(!producer.execute({7, ProducerKind::End}, 35, 1, links));
  auto final = links.groups.take(40);
  CHECK(final && final->value.events.empty() && final->value.point.interval == 3);
  links.replies.publish(40, 1, {2});
  producer.receive(45, 1, links);
  CHECK(producer.execute({7, ProducerKind::End}, 45, 1, links) == 0);
  CHECK(producer.closed() && links.closure.take(60)->value.last_label == 2);
  faults(ErrorCode::Protocol,
         [&] { (void)producer.execute({8, ProducerKind::Flush}, 50, 1, links); });
}
void capacity_test() {
  auto p = default_profile();
  p.result_slots = 1;
  Scoreboard slots(p);
  Trace trace;
  ControlLinks links(p);
  TimelineProducer producer(p, slots, trace, [](const ActionSpec &) {});
  const auto handle = producer.execute({1, ProducerKind::Append, 0, 4}, 0, 1, links);
  CHECK(handle && *handle != 0);
  faults(ErrorCode::Capacity,
         [&] { (void)producer.execute({2, ProducerKind::Append, 1, 4}, 5, 1, links); });
  CHECK(trace.events().size() == 1);
  producer.reset();
  slots.reset();
  CHECK(producer.execute({1, ProducerKind::Append, 0, 1}, 10, 2, links) == 0);
  faults(ErrorCode::Capacity,
         [&] { (void)producer.execute({2, ProducerKind::Append, 0, 2}, 15, 2, links); });
  CHECK(trace.events().size() == 2);
}
void calendar_test() {
  auto p = default_profile();
  ScriptedBackend backend;
  Trace trace;
  DeviceRuntime device(p, backend, trace);
  auto first = launch(p, 0, 1, 100);
  device.accept(first);
  const auto calls = backend.calls();
  auto conflict = launch(p, 0, 2, 110, 2);
  auto independent = launch(p, 1, 1, 110, 3);
  conflict.events.push_back(independent.events.front());
  conflict.events.back().label = conflict.label;
  faults(ErrorCode::ResourceConflict, [&] { device.accept(conflict); });
  CHECK(device.calendar().reservations().size() == 1 && backend.calls() == calls);
  CHECK(trace.events().size() == 1);
  device.accept(launch(p, 0, 1, 120, 4));
  CHECK(device.calendar().reservations().size() == 2);
}
void readout_test() {
  auto p = default_profile();
  auto &map = p.mappings[3];
  CHECK(map.actions[0].kind == ActionKind::Acquire);
  map.actions[0].separate_arm = true;
  map.actions[0].discriminator_delay = 0;
  ActionSpec arm = map.actions[0];
  arm.kind = ActionKind::DiscriminatorArm;
  arm.port = 2;
  arm.resources = {{20, true}};
  arm.delay = 80;
  arm.duration = 1;
  map.actions.push_back(arm);
  p.validate();
  Scoreboard slots(p);
  const auto token = slots.reserve(1, 0);
  ScriptedBackend backend({{token.measurement, true}});
  Trace trace;
  ControlLinks links(p);
  DeviceRuntime device(p, backend, trace);
  auto batch = launch(p, 0, 4, 100, 1, token);
  auto corrupted = batch;
  ++corrupted.events.back().token->generation;
  faults(ErrorCode::InvalidToken, [&] { device.accept(corrupted); });
  CHECK(device.drained() && device.calendar().reservations().empty());
  device.accept(batch);
  for (Tick tick : {100U, 140U, 180U, 181U})
    device.process(tick, 1, links);
  CHECK(!links.cpu_results.take(180));
  const auto cpu = links.cpu_results.take(185);
  CHECK(cpu && cpu->value.value && cpu->value.token == token);
  CHECK(!links.fast_results.take(200));
  CHECK(links.fast_results.take(220)->value.token == token);
  CHECK(device.drained());
  std::vector<Tick> times;
  for (const auto &e : trace.events())
    if (e.kind == "MeasurementSampled" || e.kind == "ResultReady")
      times.push_back(e.tick);
  CHECK(times == std::vector<Tick>({140, 180}));
  p.mappings[3].actions.back().targets = {1};
  faults(ErrorCode::InvalidProfile, [&] { p.validate(); });
}
void overflow_test() {
  auto p = default_profile();
  p.mappings[3].actions[0].duration = 1;
  p.mappings[3].actions[0].discriminator_delay = 10;
  ScriptedBackend backend;
  Trace trace;
  DeviceRuntime device(p, backend, trace);
  Scoreboard slots(p);
  const auto token = slots.reserve(1, 0);
  const auto calls = backend.calls();
  faults(ErrorCode::TimeOverflow,
         [&] { device.accept(launch(p, 0, 4, std::numeric_limits<Tick>::max() - 5, 1, token)); });
  CHECK(device.drained() && device.calendar().reservations().empty());
  CHECK(trace.events().empty() && backend.calls() == calls);
  faults(ErrorCode::TimeOverflow,
         [] { (void)Clock{1, 0}.after(std::numeric_limits<Tick>::max()); });
  faults(ErrorCode::InvalidImage, [] { ProgramImage image(0xffffffffU, 0xffffffffU); });
}
void reset_test() {
  auto p = default_profile();
  Scoreboard slots(p);
  ScriptedBackend backend;
  Trace trace;
  ControlLinks links(p);
  DeviceRuntime device(p, backend, trace);
  device.accept(launch(p, 0, 4, 100, 1, slots.reserve(1, 0)));
  device.process(100, 1, links);
  device.reset(140, 2);
  CHECK(device.drained() && links.cpu_results.empty());
  CHECK(trace.events().back().kind == "ResetAborted");
  device.process(140, 2, links);
  CHECK(links.cpu_results.empty());
}
void sample_collision_test() {
  auto p = default_profile();
  ScriptedBackend backend;
  Trace trace;
  DeviceRuntime device(p, backend, trace);
  Scoreboard slots(p);
  device.accept(launch(p, 0, 4, 100, 1, slots.reserve(1, 0)));
  faults(ErrorCode::ResourceConflict, [&] { device.accept(launch(p, 0, 1, 140, 2)); });
  CHECK(device.calendar().reservations().size() == 1);
}
void history_test() {
  auto p = default_profile();
  p.history_depth = 1;
  Scoreboard slots(p);
  FastHistory history(p);
  const auto first = slots.reserve(1, 0);
  const auto other = slots.reserve(1, 1);
  const auto last = slots.reserve(1, 0);
  history.commit({other, true}, 1);
  history.commit({first, false}, 1);
  CHECK(history.evaluate({other, true}));
  CHECK(history.evaluate({first, false}));
  history.commit({last, true}, 1);
  faults(ErrorCode::InvalidToken, [&] { (void)history.evaluate({first, false}); });
  faults(ErrorCode::Protocol, [&] { history.commit({last, true}, 1); });
  history.reset();
  history.commit({last, true}, 2);
  faults(ErrorCode::InvalidToken, [&] { (void)history.evaluate({last, true}); });
}
int main(int argc, char **argv) {
  try {
    const std::map<std::string, std::function<void()>> tests{
        {"producer", producer_test},
        {"capacity", capacity_test},
        {"calendar", calendar_test},
        {"readout", readout_test},
        {"overflow", overflow_test},
        {"reset", reset_test},
        {"sample_collision", sample_collision_test},
        {"history", history_test}};
    CHECK(argc == 2);
    tests.at(argv[1])();
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
