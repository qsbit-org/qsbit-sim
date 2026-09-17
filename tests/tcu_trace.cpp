#include "qsbit/mailbox.hpp"
#include "qsbit/tcu.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <systemc>

using namespace qsbit;
using namespace sc_core;

// Generic timing-component driver. It knows neither an external ISA nor an oracle.
struct TimingHarness : sc_module {
  Profile profile;
  Trace trace;
  TcuCycleModel tcu;
  Mailbox<Group> requests;
  Mailbox<GroupReply> replies;
  Mailbox<EndOfStream> closure;
  sc_clock clock;
  std::vector<Tick> intervals;
  std::string failure;
  bool complete = false;
  TimingHarness(sc_module_name name, Tick start, std::vector<Tick> values)
      : sc_module(name), profile(), tcu(profile, trace), requests(1, profile.tcu),
        replies(1, profile.cpu), closure(1, profile.tcu), clock("tcu_clock", 20, SC_NS),
        intervals(std::move(values)) {
    profile.start = start;
    profile.validate();
    tcu.reset();
    SC_METHOD(edge);
    sensitive << clock.posedge_event();
    dont_initialize();
    SC_THREAD(produce);
  }
  void edge() {
    try {
      const auto now = sc_time_stamp().value();
      const auto *pending = requests.peek(now);
      auto out =
          tcu.step(now, 1, pending ? &pending->value : nullptr, {}, [](const LaunchBatch &) {});
      if (out.admitted) {
        const auto message = requests.take(now);
        replies.publish(now, 1, GroupReply{message->value.point.label});
      }
      if (auto end = closure.take(now))
        tcu.close(end->value);
      if (tcu.drained()) {
        complete = true;
        sc_stop();
      }
    } catch (const std::exception &e) {
      failure = e.what();
      sc_stop();
    }
  }
  void produce() {
    try {
      wait(1, SC_NS);
      Id label = 0;
      for (auto interval : intervals) {
        Group group;
        group.point = {1, ++label, interval, {}};
        group.configuration = profile.fingerprint();
        requests.publish(sc_time_stamp().value(), 1, group);
        do {
          wait(5, SC_NS);
        } while (!replies.peek(sc_time_stamp().value()));
        auto reply = replies.take(sc_time_stamp().value());
        require(reply->value.label == label, ErrorCode::Protocol, "wrong timing reply");
      }
      closure.publish(sc_time_stamp().value(), 1, EndOfStream{label});
    } catch (const std::exception &e) {
      failure = e.what();
      sc_stop();
    }
  }
};
int sc_main(int argc, char **argv) {
  try {
    require(argc >= 4, ErrorCode::InvalidOperand, "usage: tcu_trace OUTPUT START INTERVAL...");
    sc_set_time_resolution(1, SC_NS);
    std::vector<Tick> intervals;
    for (int i = 3; i < argc; ++i)
      intervals.push_back(std::stoull(argv[i]));
    TimingHarness harness("harness", std::stoull(argv[2]), std::move(intervals));
    sc_start(sc_time(1000000, SC_NS));
    std::ofstream trace(argv[1]);
    harness.trace.write_jsonl(trace);
    require(harness.failure.empty(), ErrorCode::Protocol, harness.failure);
    require(harness.complete, ErrorCode::Watchdog, "timing component did not drain");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
