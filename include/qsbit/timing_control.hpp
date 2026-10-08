#pragma once

#include "qsbit/control_protocol.hpp"
#include "qsbit/feedback.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/mailbox.hpp"
#include "qsbit/trace.hpp"
#include <functional>

namespace qsbit {
enum class ControlKind { Codeword, Wait, FetchMeasurement, Halt, Synchronize };
struct ControlOperation {
  Id instruction = 0;
  ControlKind kind = ControlKind::Wait;
  std::uint32_t first = 0, second = 0;
  bool operator==(const ControlOperation &) const = default;
};
[[nodiscard]] ControlOperation adapt_quantum(const rv32::Decoded &instruction, Id id,
                                             std::uint32_t lhs, std::uint32_t rhs);
struct ControlLinks {
  Mailbox<TimingEvents> timing_events;
  Mailbox<EnqueueReply> replies;
  Mailbox<EndOfStream> closure;
  Mailbox<Completion> cpu_results, fast_results;
  Mailbox<MeasurementReference> fast_credits;
  explicit ControlLinks(const Profile &p)
      : timing_events(1, p.tcu, p.command_latency), replies(1, p.cpu, p.reply_latency),
        closure(1, p.tcu, p.command_latency),
        cpu_results(p.result_capacity, p.cpu, p.cpu_result_latency),
        fast_results(p.result_capacity, p.tcu, p.fast_result_latency),
        fast_credits(p.result_capacity, p.cpu) {}
  void reset();
};
class TimingControl {
public:
  using ValidateAction = std::function<void(const EventSpec &)>;
  using ValidateSync = std::function<void(std::uint32_t)>;
  TimingControl(const Profile &profile, MeasurementRegisters &registers, Trace &trace,
                ValidateAction validate, ValidateSync validate_sync = {});
  void receive(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> execute(const ControlOperation &operation, Tick now, Epoch epoch,
                                       ControlLinks &links);
  void reset();
  [[nodiscard]] Tick time_point() const { return time_point_; }
  [[nodiscard]] bool closed() const { return closed_; }
  [[nodiscard]] bool pending() const { return enqueue_request_.has_value(); }

private:
  bool enqueue(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> execute_zero_wait(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> advance_time(Tick interval, Tick now, Epoch epoch,
                                            ControlLinks &links);
  std::optional<std::uint32_t> codeword(const ControlOperation &operation, Tick now, Epoch epoch);
  const Profile &profile_;
  MeasurementRegisters &measurement_registers_;
  Trace &trace_;
  ValidateAction validate_;
  ValidateSync validate_sync_;
  std::vector<OperationEvent> pending_events_;
  std::vector<std::uint32_t> pending_sync_;
  std::optional<TimingEvents> enqueue_request_;
  std::optional<ControlOperation> held_;
  Tick time_point_ = 0, last_enqueued_time_ = 0;
  Id last_label_ = 0, next_event_ = 1;
  bool pending_point_ = false, enqueued_ = false, closed_ = false;
  bool pending_wait_for_next_ = false, allow_same_time_ = false;
};
} // namespace qsbit
