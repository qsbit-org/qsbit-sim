#pragma once

#include "qsbit/control.hpp"
#include "qsbit/feedback.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/mailbox.hpp"
#include "qsbit/trace.hpp"
#include <functional>

namespace qsbit {
enum class ControlKind { Append, Advance, Flush, ReadResult, End, ConditionalAppend, Synchronize };
struct ControlOperation {
  Id instruction = 0;
  ControlKind kind = ControlKind::Flush;
  std::uint32_t first = 0, second = 0, condition_handle = 0;
  bool expected = false;
  bool operator==(const ControlOperation &) const = default;
};
[[nodiscard]] ControlOperation adapt_quantum(const rv32::Decoded &instruction, Id id,
                                             std::uint32_t lhs, std::uint32_t rhs,
                                             std::uint32_t predicate_handle);
struct ControlLinks {
  Mailbox<TimingEvents> groups;
  Mailbox<EnqueueReply> replies;
  Mailbox<EndOfStream> closure;
  Mailbox<Completion> cpu_results, fast_results;
  Mailbox<MeasurementReference> fast_credits;
  explicit ControlLinks(const Profile &p)
      : groups(1, p.tcu, p.command_latency), replies(1, p.cpu, p.reply_latency),
        closure(1, p.tcu, p.command_latency),
        cpu_results(p.result_slots, p.cpu, p.cpu_result_latency),
        fast_results(p.result_slots, p.tcu, p.fast_result_latency),
        fast_credits(p.result_slots, p.cpu) {}
  void reset();
};
// CPU-side reserve phase. The TCU owns the timing and event queues.
class TimingControl {
public:
  using ValidateAction = std::function<void(const EventSpec &)>;
  TimingControl(const Profile &profile, MeasurementResults &scoreboard, Trace &trace,
                ValidateAction validate);
  void receive(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> execute(const ControlOperation &operation, Tick now, Epoch epoch,
                                       ControlLinks &links);
  void reset();
  [[nodiscard]] Tick time_point() const { return time_point_; }
  [[nodiscard]] Tick cursor() const { return time_point(); } // Compatibility accessor.
  [[nodiscard]] bool closed() const { return closed_; }
  [[nodiscard]] bool pending() const { return enqueue_request_.has_value(); }

private:
  bool flush(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> append(const ControlOperation &operation, Tick now, Epoch epoch);
  const Profile &profile_;
  MeasurementResults &measurement_results_;
  Trace &trace_;
  ValidateAction validate_;
  std::vector<OperationEvent> pending_events_;
  std::optional<TimingEvents> enqueue_request_;
  std::optional<ControlOperation> held_;
  Tick time_point_ = 0, last_enqueued_time_ = 0;
  Id last_label_ = 0, next_event_ = 1;
  bool open_ = false, flushed_ = false, closed_ = false;
};
using ProducerKind = ControlKind;
using ProducerOperation = ControlOperation;
using TimelineProducer = TimingControl;
} // namespace qsbit
