#include "qsbit/defaults.hpp"
#include "qsbit/profile.hpp"
#include "qsbit/sync.hpp"
#include "qsbit/tcu.hpp"
#include "qsbit/timing_config.hpp"
#include "test.hpp"
#include <array>

using namespace qsbit;
int main() {
  try {
    for (bool reverse : {false, true}) {
      const std::array<SyncConnection, 1> connections{{{1, 2, 8, 6, 4}}};
      SyncNetwork network({4, 0}, connections);
      Trace trace;
      SyncUnit first(1, network, trace), second(2, network, trace);
      const std::array<std::uint32_t, 1> to_first{1}, to_second{2};
      first.book(to_second, 0, 1);
      for (Tick tick = 0; tick <= 64; tick += 4) {
        if (tick == 40)
          second.book(to_first, tick, 1);
        bool a = false, b = false;
        if (reverse) {
          b = second.step(tick, 1);
          a = first.step(tick, 1);
        } else {
          a = first.step(tick, 1);
          b = second.step(tick, 1);
        }
        CHECK(a == (tick >= 32 && tick < 64));
        CHECK(!b);
      }
      CHECK(first.drained() && second.drained() && network.empty());
      unsigned completed = 0;
      for (const auto &event : trace.events())
        if (event.kind == "SyncCompleted") {
          CHECK(event.tick == 64);
          ++completed;
        }
      CHECK(completed == 2);
      // A new synchronization cannot consume a signal from the previous one.
      first.book(to_second, 80, 1);
      CHECK(first.step(112, 1));
      first.reset();
      second.reset();
      network.reset();
      CHECK(first.drained() && network.empty());
      faults(ErrorCode::UnsupportedSynchronization, [&] { first.validate(3); });
    }
    const std::array<SyncConnection, 1> connection{{{1, 2, 1, 1, 1}}};
    SyncNetwork network({4, 0}, connection);
    network.send(1, 2, 0);
    faults(ErrorCode::Capacity, [&] { network.send(1, 2, 4); });
    CHECK(network.receive(1, 2, 4) == 4);
    faults(ErrorCode::Capacity, [&] { network.send(1, 2, 4); });
    network.send(1, 2, 8);
    Trace trace;
    SyncUnit unit(2, network, trace);
    const std::array<std::uint32_t, 1> target{1};
    unit.book(target, 8, 1);
    faults(ErrorCode::UnsupportedSynchronization, [&] { unit.book(target, 8, 1); });
    CHECK(!unit.step(12, 1) && unit.drained());
    auto profile = default_profile();
    Trace tcu_trace;
    TcuCycleModel tcu(tcu_config(profile), tcu_trace, 1);
    TimingEvents a{
        {1, 1, TcuCycle{8}, {}, {1}, UnderflowPolicy::Strict}, {}, profile.fingerprint()};
    TimingEvents b{
        {1, 2, TcuCycle{8}, {}, {1}, UnderflowPolicy::Strict}, {}, profile.fingerprint()};
    const auto check = [](const TriggeredEvents &) {};
    CHECK(tcu.step(0, 1, &a, {}, check).admitted);
    CHECK(!tcu.step(20, 1, &b, {}, check).admitted);
    CHECK(!tcu.step(1160, 1, &b, {}, check, false, [](std::span<const std::uint32_t>) {}).admitted);
    CHECK(tcu.step(1180, 1, &b, {}, check).admitted);
    std::cout << "PASS neighbor synchronization\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
