#pragma once

#include "qsbit/control_command.hpp"
#include "qsbit/control_protocol.hpp"
#include "qsbit/feedback.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/mailbox.hpp"
#include "qsbit/timing_config.hpp"
#include "qsbit/trace.hpp"
#include <functional>

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
  void reset();
};
class TimingControl {
public:
  using ValidateAction = std::function<void(const EventSpec &)>;
  using ValidateSync = std::function<void(std::uint32_t)>;
  TimingControl(TimingConfig profile, MeasurementRegisters &registers, Trace &trace,
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
  const TimingConfig profile_;
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
