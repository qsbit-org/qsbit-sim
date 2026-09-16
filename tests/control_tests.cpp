#include "qsbit/feedback.hpp"
#include "qsbit/tcu.hpp"
#include "test.hpp"
#include <functional>
#include <map>

using namespace qsbit;
Profile profile() {
  Profile p;
  p.start = 100;
  p.ports = 2;
  p.qubits = 2;
  p.timing_capacity = 1;
  p.event_capacity = 1;
  ActionSpec a;
  a.port = 0;
  a.targets = {0};
  a.resources = {{0, true}};
  ActionSpec b = a;
  b.port = 1;
  b.targets = {1};
  b.resources = {{1, true}};
  p.mappings = {{0, 1, {a}}, {1, 1, {b}}};
  p.validate();
  return p;
}
Group group(const Profile &p, Id label, Tick interval, std::initializer_list<std::uint32_t> ports) {
  Group g;
  g.point = {1, label, interval, {}};
  g.configuration = p.fingerprint();
  for (auto port : ports) {
    auto e = lower(p, port, 1, 1, label, label * 100 + port).front();
    e.label = label;
    g.point.manifest.push_back(e.id);
    g.events.push_back(e);
  }
  return g;
}
void admission_test() {
  auto p = profile();
  Trace trace;
  TcuCycleModel tcu(p, trace);
  const auto ok = [](const LaunchBatch &) {};
  auto first = group(p, 1, 0, {0, 1});
  auto second = group(p, 2, 2, {0});
  CHECK(tcu.step(20, 1, &first, {}, ok).admitted);
  CHECK(tcu.port_size(0) == 1 && tcu.port_size(1) == 1);
  CHECK(!tcu.step(40, 1, &second, {}, ok).admitted);
  const auto out = tcu.step(100, 1, &second, {}, ok);
  CHECK(out.launch && out.launch->events.size() == 2 && !out.admitted);
  CHECK(tcu.timing_size() == 0 && tcu.port_size(0) == 0);
  CHECK(tcu.step(120, 1, &second, {}, ok).admitted);
  CHECK(tcu.step(140, 1, nullptr, {}, ok).launch->events.size() == 1);
  tcu.close({2});
  CHECK(tcu.drained());
}
void atomic_test() {
  auto p = profile();
  Trace trace;
  TcuCycleModel tcu(p, trace);
  auto first = group(p, 1, 0, {0, 1});
  const auto ok = [](const LaunchBatch &) {};
  auto broken = first;
  broken.point.manifest.pop_back();
  faults(ErrorCode::ManifestMismatch, [&] { (void)tcu.step(20, 1, &broken, {}, ok); });
  CHECK(tcu.timing_size() == 0 && tcu.port_size(0) == 0);
  CHECK(tcu.step(20, 1, &first, {}, ok).admitted);
  faults(ErrorCode::ResourceConflict, [&] {
    (void)tcu.step(100, 1, nullptr, {}, [](const LaunchBatch &) {
      throw Fault(ErrorCode::ResourceConflict, "injected whole-batch preflight rejection");
    });
  });
  CHECK(tcu.timing_size() == 1 && tcu.port_size(0) == 1 && tcu.port_size(1) == 1);
  CHECK(trace.events().size() == 1);
  // A fatal candidate admission must also suppress an otherwise valid due launch.
  auto late = group(p, 2, 0, {0});
  faults(ErrorCode::Protocol, [&] { (void)tcu.step(100, 1, &late, {}, ok); });
  CHECK(tcu.timing_size() == 1 && trace.events().size() == 1);
}
void empty_test() {
  auto p = profile();
  Trace trace;
  TcuCycleModel tcu(p, trace);
  const auto ok = [](const LaunchBatch &) {};
  CHECK(!tcu.step(100, 1, nullptr, {}, ok).launch);
  auto future = group(p, 1, 4, {0});
  CHECK(tcu.step(120, 1, &future, {}, ok).admitted);
  CHECK(tcu.step(180, 1, nullptr, {}, ok).launch.has_value());
  auto next = group(p, 2, 4, {1});
  CHECK(tcu.step(240, 1, &next, {}, ok).admitted);
  CHECK(tcu.step(260, 1, nullptr, {}, ok).launch->fire_tick == 260);
  auto late = group(p, 3, 1, {0});
  faults(ErrorCode::LateAdmission, [&] { (void)tcu.step(280, 1, &late, {}, ok); });
  tcu.reset();
  CHECK(tcu.last_label() == 0 && !tcu.drained());
}
void scoreboard_test() {
  auto p = profile();
  p.result_slots = 1;
  Scoreboard scoreboard(p);
  const auto token = scoreboard.reserve(1, 0);
  CHECK(!scoreboard.consume(token.handle, 1));
  scoreboard.deliver({token, true}, 1);
  faults(ErrorCode::DuplicateResult, [&] { scoreboard.deliver({token, true}, 1); });
  CHECK(scoreboard.consume(token.handle, 1) == true);
  CHECK(scoreboard.deliveries_pending() && !scoreboard.has_fast_credit());
  faults(ErrorCode::InvalidToken, [&] { (void)scoreboard.consume(token.handle, 1); });
  scoreboard.acknowledge_fast(token, 1);
  CHECK(!scoreboard.deliveries_pending());
  const auto later = scoreboard.reserve(1, 0);
  CHECK(later.generation > token.generation && later.handle != token.handle);
  faults(ErrorCode::InvalidToken, [&] { scoreboard.deliver({token, false}, 1); });
  scoreboard.reset();
  const auto reset_token = scoreboard.reserve(2, 0);
  scoreboard.deliver({later, true}, 2);
  CHECK(!scoreboard.consume(reset_token.handle, 2));
}
void fast_test() {
  auto p = profile();
  Trace trace;
  TcuCycleModel tcu(p, trace);
  Scoreboard scoreboard(p);
  const auto token = scoreboard.reserve(1, 0);
  const auto ok = [](const LaunchBatch &) {};
  auto conditional = group(p, 1, 0, {1});
  conditional.events[0].condition = Condition{token, true};
  CHECK(tcu.step(20, 1, &conditional, {}, ok).admitted);
  faults(ErrorCode::InvalidToken, [&] { (void)tcu.step(100, 1, nullptr, {{token, true}}, ok); });
  CHECK(tcu.timing_size() == 1);
  tcu.reset();
  CHECK(tcu.step(20, 1, &conditional, {}, ok).admitted);
  CHECK(tcu.step(80, 1, nullptr, {{token, false}}, ok).fast_delivered.size() == 1);
  const auto out = tcu.step(100, 1, nullptr, {}, ok);
  CHECK(out.launch && out.launch->events.empty());
  CHECK(trace.events().back().kind == "ConditionCancelled");
}
void mapping_test() {
  auto p = profile();
  faults(ErrorCode::InvalidPort, [&] { (void)p.mapping(2, 1); });
  faults(ErrorCode::InvalidCodeword, [&] { (void)p.mapping(0, 2); });
  auto g = group(p, 1, 1, {0, 0});
  faults(ErrorCode::ManifestMismatch, [&] { validate_group(g, p); });
  const auto before = p.fingerprint();
  p.cpu_result_latency = 2;
  CHECK(p.fingerprint() != before);
  auto bad = p;
  bad.mappings.push_back(bad.mappings[0]);
  faults(ErrorCode::InvalidProfile, [&] { bad.validate(); });
}
int main(int argc, char **argv) {
  try {
    const std::map<std::string, std::function<void()>> tests{
        {"admission", admission_test},   {"atomic", atomic_test}, {"empty", empty_test},
        {"scoreboard", scoreboard_test}, {"fast", fast_test},     {"mapping", mapping_test}};
    CHECK(argc == 2);
    tests.at(argv[1])();
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
