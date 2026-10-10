#include "qsbit/feedback.hpp"
#include "qsbit/profile.hpp"
#include "qsbit/tcu.hpp"
#include "qsbit/timing_config.hpp"
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
  EventSpec a;
  a.port = 0;
  a.get<GateSpec>().operands.targets = {0};
  a.get<GateSpec>().operands.resources = {{0, true}};
  EventSpec b = a;
  b.port = 1;
  b.get<GateSpec>().operands.targets = {1};
  b.get<GateSpec>().operands.resources = {{1, true}};
  p.mappings = {{0, 1, {a}}, {1, 1, {b}}};
  p.validate();
  return p;
}
TimingEvents group(const Profile &p, Id label, Tick interval,
                   std::initializer_list<std::uint32_t> ports) {
  TimingEvents g;
  g.point = {1, label, TcuCycle{interval}, {}};
  g.point.underflow_policy = interval > 0 ? UnderflowPolicy::Strict : UnderflowPolicy::Inherit;
  g.configuration = p.fingerprint();
  for (auto port : ports) {
    auto e = decode_codeword(timing_config(p), port, 1, 1, label, label * 100 + port).front();
    e.label = label;
    g.point.manifest.push_back(e.id);
    g.events.push_back(e);
  }
  return g;
}
void admission_test() {
  auto p = profile();
  Trace trace;
  TcuCycleModel tcu(tcu_config(p), trace);
  const auto ok = [](const TriggeredEvents &) {};
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
  p.timing_capacity = 2;
  p.event_capacity = 2;
  Trace trace;
  TcuCycleModel tcu(tcu_config(p), trace);
  auto first = group(p, 1, 0, {0, 1});
  const auto ok = [](const TriggeredEvents &) {};
  auto broken = first;
  broken.point.manifest.pop_back();
  faults(ErrorCode::ManifestMismatch, [&] { (void)tcu.step(20, 1, &broken, {}, ok); });
  CHECK(tcu.timing_size() == 0 && tcu.port_size(0) == 0);
  CHECK(tcu.step(20, 1, &first, {}, ok).admitted);
  faults(ErrorCode::ResourceConflict, [&] {
    (void)tcu.step(100, 1, nullptr, {}, [](const TriggeredEvents &) {
      throw Fault(ErrorCode::ResourceConflict, "injected whole-batch preflight rejection");
    });
  });
  CHECK(tcu.timing_size() == 1 && tcu.port_size(0) == 1 && tcu.port_size(1) == 1);
  CHECK(trace.events().size() == 1);
  // A fatal candidate admission must also suppress an otherwise valid due launch.
  auto late = group(p, 2, 0, {0});
  faults(ErrorCode::Protocol, [&] { (void)tcu.step(100, 1, &late, {}, ok); });
  CHECK(tcu.timing_size() == 1 && trace.events().size() == 1);
  auto second = group(p, 2, 2, {0});
  const Completion result{{1, 1, 0}, true};
  const Completion invalid{{1, 2, p.qubits}, true};
  faults(ErrorCode::InvalidMeasurement,
         [&] { (void)tcu.step(100, 1, &second, {result, invalid}, ok); });
  CHECK(tcu.timing_size() == 1 && tcu.port_size(0) == 1 && tcu.port_size(1) == 1);
  CHECK(tcu.last_label() == 1 && trace.events().size() == 1);
  CHECK(!tcu.execution_flags().evaluate(0, ExecutionFlag::LastOne));
  const auto out = tcu.step(100, 1, &second, {result}, ok);
  CHECK(out.launch && out.launch->events.size() == 2 && out.admitted);
  CHECK(out.fast_delivered.size() == 1);
  CHECK(tcu.step(140, 1, nullptr, {}, ok).launch->label == 2);
}
void empty_test() {
  auto p = profile();
  Trace trace;
  TcuCycleModel tcu(tcu_config(p), trace);
  const auto ok = [](const TriggeredEvents &) {};
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
void registers_test() {
  auto p = profile();
  p.result_capacity = 2;
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  CHECK(registers.read(0) == false);
  const auto first = registers.reserve(1, 0);
  const auto second = registers.reserve(1, 0);
  CHECK(!registers.has_capacity() && !registers.read(0));
  CHECK(registers.read(1) == false);
  faults(ErrorCode::Protocol, [&] { registers.deliver({second, true}, 1); });
  CHECK(registers.registers()[0].pending == 2);
  auto corrupt = first;
  corrupt.target = 1;
  faults(ErrorCode::InvalidMeasurement, [&] { registers.deliver({corrupt, true}, 1); });
  registers.deliver({first, true}, 1);
  CHECK(!registers.read(0) && registers.registers()[0].pending == 1);
  faults(ErrorCode::DuplicateResult, [&] { registers.deliver({first, true}, 1); });
  registers.deliver({second, false}, 1);
  CHECK(registers.read(0) == false);
  CHECK(registers.deliveries_pending() && !registers.has_capacity());
  registers.acknowledge_fast(first, 1);
  registers.acknowledge_fast(second, 1);
  CHECK(!registers.deliveries_pending() && registers.has_capacity());
  const auto later = registers.reserve(1, 0);
  CHECK(later.measurement > second.measurement && !registers.read(0));
  registers.reset();
  CHECK(registers.read(0) == false);
  const auto reset_reference = registers.reserve(2, 0);
  registers.deliver({later, true}, 2);
  CHECK(!registers.read(0));
  registers.deliver({reset_reference, true}, 2);
  CHECK(registers.read(0) == true);
  faults(ErrorCode::InvalidOperand, [&] { (void)registers.read(2); });
}
void fast_test() {
  auto p = profile();
  p.mappings[0].actions[0].get<GateSpec>().execution_flag = ExecutionFlag::LastOne;
  Trace trace;
  TcuCycleModel tcu(tcu_config(p), trace);
  MeasurementRegisters registers({p.qubits, p.result_capacity, p.fast_feedback});
  const auto reference = registers.reserve(1, 0);
  const auto ok = [](const TriggeredEvents &) {};
  auto conditional = group(p, 1, 0, {0});
  CHECK(tcu.step(20, 1, &conditional, {}, ok).admitted);
  const auto same_edge = tcu.step(100, 1, nullptr, {{reference, true}}, ok);
  CHECK(same_edge.launch && same_edge.launch->events.empty());
  CHECK(tcu.execution_flags().evaluate(0, ExecutionFlag::LastOne));
  tcu.reset();
  trace = Trace{};
  CHECK(tcu.step(20, 1, &conditional, {}, ok).admitted);
  CHECK(tcu.step(80, 1, nullptr, {{reference, false}}, ok).fast_delivered.size() == 1);
  const auto out = tcu.step(100, 1, nullptr, {}, ok);
  CHECK(out.launch && out.launch->events.empty());
  CHECK(trace.events().back().kind == "ConditionCancelled");
  tcu.reset();
  trace = Trace{};
  CHECK(tcu.step(20, 1, &conditional, {}, ok).admitted);
  CHECK(tcu.step(80, 1, nullptr, {{reference, true}}, ok).fast_delivered.size() == 1);
  CHECK(tcu.step(100, 1, nullptr, {}, ok).launch->events.size() == 1);
}
void mapping_test() {
  auto p = profile();
  faults(ErrorCode::InvalidPort, [&] { (void)p.mapping(2, 1); });
  faults(ErrorCode::InvalidCodeword, [&] { (void)p.mapping(0, 2); });
  auto g = group(p, 1, 1, {0, 0});
  faults(ErrorCode::ManifestMismatch, [&] { validate_timing_events(g, timing_config(p)); });
  const auto before = p.fingerprint();
  p.cpu_result_latency = 2;
  CHECK(p.fingerprint() != before);
  auto bad = p;
  bad.mappings.push_back(bad.mappings[0]);
  faults(ErrorCode::InvalidProfile, [&] { bad.validate(); });
  const auto unconditional = p.fingerprint();
  p.mappings[0].actions[0].get<GateSpec>().execution_flag = ExecutionFlag::LastOne;
  p.validate();
  CHECK(p.fingerprint() != unconditional);
  auto two_qubit = p;
  two_qubit.mappings[0].actions[0].get<GateSpec>().operands.targets = {0, 1};
  faults(ErrorCode::InvalidProfile, [&] { two_qubit.validate(); });
  auto invalid_flag = p;
  invalid_flag.mappings[0].actions[0].get<GateSpec>().execution_flag =
      static_cast<ExecutionFlag>(4);
  faults(ErrorCode::InvalidProfile, [&] { invalid_flag.validate(); });
}
int main(int argc, char **argv) {
  try {
    const std::map<std::string, std::function<void()>> tests{
        {"admission", admission_test}, {"atomic", atomic_test}, {"empty", empty_test},
        {"registers", registers_test}, {"fast", fast_test},     {"mapping", mapping_test}};
    CHECK(argc == 2);
    tests.at(argv[1])();
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
