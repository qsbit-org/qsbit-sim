#include "qsbit/defaults.hpp"
#include "qsbit/device.hpp"
#include "test.hpp"
#include <algorithm>

using namespace qsbit;

class RecordingBackend final : public IQuantumBackend {
public:
  void validate(const EventSpec &event) const override {
    CHECK(event.kind() != ActionKind::GateOutput);
    CHECK(!event.targets().empty());
  }
  void reset(std::uint32_t, std::uint32_t) override {
    gates.clear();
    mutations = 0;
  }
  std::vector<bool> execute(Epoch, std::span<const BackendOperation> operations) override {
    ++mutations;
    std::vector<bool> results;
    for (const auto &operation : operations) {
      if (const auto *application = std::get_if<BackendGates>(&operation))
        gates.insert(gates.end(), application->gates.begin(), application->gates.end());
      if (const auto *measurement = std::get_if<BackendMeasurement>(&operation))
        results.insert(results.end(), measurement->references.size(), false);
    }
    return results;
  }
  std::vector<EventSpec> gates;
  unsigned mutations = 0;
};

TriggeredEvents output(const Profile &p, unsigned port, unsigned code, Tick tick, Id id) {
  auto events = decode_codeword(p, port, code, 1, id, id);
  for (auto &event : events)
    event.label = id;
  return {1, id, tick, events};
}

void execution() {
  for (bool reverse : {false, true}) {
    auto p = default_profile();
    RecordingBackend backend;
    Trace trace;
    ControlElectronics device(p, backend, trace);
    ControlLinks links(p);
    for (Id occurrence = 1; occurrence <= 2; ++occurrence) {
      const Tick tick = occurrence * 100;
      auto first = output(p, 0, 6, tick, occurrence * 2);
      auto second = output(p, 1, 10, tick, occurrence * 2 + 1);
      device.accept(reverse ? second : first);
      device.accept(reverse ? first : second);
      CHECK(backend.gates.size() == occurrence - 1);
      device.process(tick, 1, links);
      (void)device.backend().state();
      CHECK(backend.gates.size() == occurrence);
      CHECK(backend.gates.back().operation() == "cx");
      CHECK((backend.gates.back().targets() == std::vector<std::uint32_t>{0, 1}));
      device.process(tick + 20, 1, links);
      CHECK(device.drained());
    }
    CHECK(std::count_if(trace.events().begin(), trace.events().end(),
                        [](const auto &e) { return e.kind == "GateApplied"; }) == 2);
  }
}

void rejection_and_reset() {
  auto p = default_profile();
  RecordingBackend backend;
  Trace trace;
  ControlElectronics device(p, backend, trace);
  ControlLinks links(p);
  device.accept(output(p, 0, 6, 100, 1));
  faults(ErrorCode::GateInputMismatch, [&] { device.process(100, 1, links); });
  CHECK(backend.mutations == 0 && backend.gates.empty());
  faults(ErrorCode::ResourceConflict, [&] { device.accept(output(p, 0, 6, 100, 2)); });
  faults(ErrorCode::GateInputMismatch, [&] { device.accept(output(p, 1, 10, 101, 3)); });
  faults(ErrorCode::ResourceConflict, [&] { device.accept(output(p, 1, 1, 100, 4)); });
  auto wrong = output(p, 1, 10, 100, 5);
  wrong.events.front().codeword = 99;
  faults(ErrorCode::GateInputMismatch, [&] { device.accept(wrong); });
  wrong = output(p, 1, 10, 100, 5);
  ++wrong.events.front().action.duration;
  faults(ErrorCode::GateInputMismatch, [&] { device.accept(wrong); });
  CHECK(backend.mutations == 0);
  device.reset(100, 2);
  CHECK(device.drained() && device.resources().reservations().empty());
  auto second = output(p, 1, 10, 200, 6);
  second.epoch = second.events.front().epoch = 2;
  device.accept(second);
  const auto before = backend.mutations;
  faults(ErrorCode::GateInputMismatch, [&] { device.process(200, 2, links); });
  CHECK(backend.mutations == before && backend.gates.empty());
}

void delay_alignment() {
  auto p = default_profile();
  for (auto &mapping : p.mappings)
    if (mapping.port == 0 && mapping.codeword == 6)
      mapping.actions.front().delay = 20;
  RecordingBackend backend;
  Trace trace;
  ControlElectronics device(p, backend, trace);
  ControlLinks links(p);
  device.accept(output(p, 0, 6, 100, 1));
  device.accept(output(p, 1, 10, 120, 2));
  CHECK(device.next_boundary() == 120);
  device.process(120, 1, links);
  (void)device.backend().state();
  CHECK(backend.gates.size() == 1);
  device.process(140, 1, links);
  CHECK(device.drained());
}

int main() {
  try {
    execution();
    rejection_and_reset();
    delay_alignment();
    std::cout << "PASS two-qubit gate outputs\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
