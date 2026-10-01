#include "qsbit/defaults.hpp"
#include "qsbit/device.hpp"
#include "test.hpp"
#include <type_traits>

using namespace qsbit;

// Existing adapters must still compile against the renamed implementation types.
static_assert(std::is_same_v<TimelineProducer, TimingControl>);
static_assert(std::is_same_v<ProducerOperation, ControlOperation>);
static_assert(std::is_same_v<ProducerKind, ControlKind>);
static_assert(std::is_same_v<Scoreboard, MeasurementResults>);
static_assert(std::is_same_v<FastHistory, ConditionalResults>);
static_assert(std::is_same_v<ResourceCalendar, ResourceReservations>);
static_assert(std::is_same_v<DeviceRuntime, ControlElectronics>);
static_assert(std::is_same_v<Group, TimingEvents>);
static_assert(std::is_same_v<GroupReply, EnqueueReply>);
static_assert(std::is_same_v<ReservedEvent, OperationEvent>);
static_assert(std::is_same_v<LaunchBatch, TriggeredEvents>);
static_assert(std::is_same_v<PhysicalAction, ScheduledEvent>);
static_assert(std::is_same_v<ActionSpec, EventSpec>);
static_assert(std::is_same_v<Token, MeasurementReference>);

int main() {
  try {
    const auto p = default_profile();
    Scoreboard results(p);
    Trace trace;
    TimelineProducer control(p, results, trace, [](const ActionSpec &) {});
    ControlLinks links(p);
    CHECK(control.execute({1, ProducerKind::Advance, 8}, 0, 1, links) == 0);
    CHECK(control.cursor() == control.time_point());
    CHECK(control.cursor() == 8);

    const auto legacy = lower(p, 0, 1, 1, 2, 1);
    const auto current = decode_codeword(p, 0, 1, 1, 2, 1);
    CHECK(legacy.size() == current.size());
    Group request;
    request.point = {1, 1, 8, {}};
    request.configuration = p.fingerprint();
    request.events = legacy;
    for (std::size_t i = 0; i < legacy.size(); ++i) {
      CHECK(legacy[i].action == current[i].action);
      CHECK(legacy[i].id == current[i].id);
      request.events[i].label = request.point.label;
      request.point.manifest.push_back(legacy[i].id);
    }
    validate_group(request, p);
    validate_timing_events(request, p);
    MockBackend backend;
    DeviceRuntime electronics(p, backend, trace);
    CHECK(&electronics.calendar() == &electronics.resources());
    std::cout << "PASS source compatibility\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
