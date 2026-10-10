#pragma once

#include "qsbit/control_protocol.hpp"
#include "qsbit/mailbox.hpp"
#include "qsbit/measurement.hpp"
#include "qsbit/transport_config.hpp"

namespace qsbit {
struct ControlLinks {
  Mailbox<TimingEvents> timing_events;
  Mailbox<EnqueueReply> replies;
  Mailbox<EndOfStream> closure;
  Mailbox<Completion> cpu_results, fast_results;
  Mailbox<MeasurementReference> fast_credits;
  explicit ControlLinks(TransportConfig p)
      : timing_events(1, p.tcu, p.command_latency), replies(1, p.cpu, p.reply_latency),
        closure(1, p.tcu, p.command_latency),
        cpu_results(p.result_capacity, p.cpu, p.cpu_result_latency),
        fast_results(p.result_capacity, p.tcu, p.fast_result_latency),
        fast_credits(p.result_capacity, p.cpu) {}
  void reset() {
    timing_events.reset();
    replies.reset();
    closure.reset();
    cpu_results.reset();
    fast_results.reset();
    fast_credits.reset();
  }
};
} // namespace qsbit
