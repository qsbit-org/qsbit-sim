#include "qsbit/defaults.hpp"
#include "qsbit/tcu.hpp"
#include "test.hpp"
#include <algorithm>

using namespace qsbit;

int main() {
  try {
    auto p = default_profile();
    p.start = 100;
    p.timing_capacity = 1;
    const auto ok = [](const TriggeredEvents &) {};
    for (auto policy : {UnderflowPolicy::Inherit, static_cast<UnderflowPolicy>(99)}) {
      TimingEvents invalid{{1, 1, 1, {}, {}, policy}, {}, p.fingerprint()};
      faults(ErrorCode::Protocol, [&] { validate_timing_events(invalid, p); });
    }
    for (bool synchronization : {false, true}) {
      Trace trace;
      TcuCycleModel tcu(p, trace);
      TimingEvents zero{{1, 1, 0, {}, {}, UnderflowPolicy::PauseWhenEmpty}, {}, p.fingerprint()};
      TimingEvents first{{1, 2, 0, {}}, {}, p.fingerprint()};
      TimingEvents next{{1, 3, 2, {}, {}, UnderflowPolicy::Strict}, {}, p.fingerprint()};
      CHECK(tcu.step(20, 1, &zero, {}, ok).admitted);
      CHECK(!tcu.step(40, 1, &first, {}, ok).admitted);
      CHECK(tcu.step(100, 1, nullptr, {}, ok).launch);
      for (Tick tick = 120; tick <= 200; tick += 20)
        CHECK(!tcu.step(tick, 1, nullptr, {}, ok, synchronization).launch);
      CHECK(tcu.step(220, 1, &first, {}, ok, synchronization).admitted);
      if (synchronization)
        CHECK(!tcu.step(240, 1, nullptr, {}, ok, true).launch);
      const Tick resume = synchronization ? 260 : 240;
      CHECK(tcu.step(resume, 1, nullptr, {}, ok).launch->fire_tick == resume);
      CHECK(tcu.step(resume + 20, 1, &next, {}, ok).admitted);
      CHECK(!tcu.step(resume + 40, 1, nullptr, {}, ok).launch);
      CHECK(tcu.step(resume + 60, 1, nullptr, {}, ok).launch);
      TimingEvents late{{1, 4, 1, {}, {}, UnderflowPolicy::Strict}, {}, p.fingerprint()};
      faults(ErrorCode::LateAdmission, [&] { (void)tcu.step(resume + 80, 1, &late, {}, ok); });
      CHECK(std::count_if(trace.events().begin(), trace.events().end(),
                          [](const auto &e) { return e.kind == "TimerPaused"; }) == 2);
      tcu.close({3});
      CHECK(tcu.drained());
      trace = Trace{};
      tcu.reset();
      CHECK(tcu.step(20, 1, &zero, {}, ok).admitted);
      CHECK(tcu.step(100, 1, nullptr, {}, ok).launch);
      CHECK(!tcu.step(120, 1, nullptr, {}, ok).launch);
      tcu.close({1});
      CHECK(tcu.drained());
      trace = Trace{};
      tcu.reset();
      TimingEvents strict{{1, 1, 0, {}}, {}, p.fingerprint()};
      faults(ErrorCode::LateAdmission, [&] { (void)tcu.step(100, 1, &strict, {}, ok); });
      faults(ErrorCode::LateAdmission, [&] { (void)tcu.step(100, 1, &zero, {}, ok); });
    }
    p.timing_capacity = 4;
    for (bool zero_first : {false, true}) {
      Trace trace;
      TcuCycleModel tcu(p, trace);
      TimingEvents first{{1,
                          1,
                          0,
                          {},
                          {},
                          zero_first ? UnderflowPolicy::PauseWhenEmpty : UnderflowPolicy::Inherit},
                         {},
                         p.fingerprint()};
      TimingEvents second{
          {1, 2, 2, {}, {}, zero_first ? UnderflowPolicy::Strict : UnderflowPolicy::PauseWhenEmpty},
          {},
          p.fingerprint()};
      TimingEvents third{{1, 3, 0, {}}, {}, p.fingerprint()};
      CHECK(tcu.step(20, 1, &first, {}, ok).admitted);
      CHECK(tcu.step(40, 1, &second, {}, ok).admitted);
      CHECK(tcu.step(100, 1, nullptr, {}, ok).launch);
      if (zero_first) {
        // The queued positive interval disallows another zero interval before it triggers.
        faults(ErrorCode::Protocol, [&] { (void)tcu.step(120, 1, &third, {}, ok); });
        CHECK(!tcu.step(120, 1, nullptr, {}, ok).launch);
      } else {
        // The queued zero wait allows admission before it permits a pause.
        CHECK(tcu.step(120, 1, &third, {}, ok).admitted);
      }
      CHECK(std::none_of(trace.events().begin(), trace.events().end(),
                         [](const auto &e) { return e.kind == "TimerPaused"; }));
      CHECK(tcu.step(140, 1, nullptr, {}, ok).launch->label == 2);
      if (!zero_first) {
        CHECK(tcu.step(160, 1, nullptr, {}, ok).launch->label == 3);
        CHECK(!tcu.step(180, 1, nullptr, {}, ok).launch);
        CHECK(trace.events().back().kind == "TimerPaused");
        tcu.close({3});
        CHECK(!tcu.step(200, 1, nullptr, {}, ok).launch);
        CHECK(tcu.drained() && trace.events().back().kind == "TimerResumed");
      }
    }
    // Prefetched work does not pause. Repeated zero waits each consume one edge.
    Trace trace;
    TcuCycleModel tcu(p, trace);
    for (Id label = 1; label <= 3; ++label) {
      TimingEvents zero{
          {1, label, 0, {}, {}, UnderflowPolicy::PauseWhenEmpty}, {}, p.fingerprint()};
      CHECK(tcu.step(label * 20, 1, &zero, {}, ok).admitted);
    }
    tcu.close({3});
    for (Tick tick = 100; tick <= 140; tick += 20)
      CHECK(tcu.step(tick, 1, nullptr, {}, ok).launch);
    CHECK(tcu.drained());
    CHECK(std::none_of(trace.events().begin(), trace.events().end(),
                       [](const auto &e) { return e.kind == "TimerPaused"; }));
    std::cout << "PASS wait zero\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
