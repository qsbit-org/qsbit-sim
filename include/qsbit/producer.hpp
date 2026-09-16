#pragma once

#include "qsbit/control.hpp"
#include "qsbit/feedback.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/mailbox.hpp"
#include "qsbit/trace.hpp"
#include <functional>

namespace qsbit {
enum class ProducerKind { Append, Advance, Flush, ReadResult, End, ConditionalAppend, Synchronize };
struct ProducerOperation {
  Id instruction = 0;
  ProducerKind kind = ProducerKind::Flush;
  std::uint32_t first = 0, second = 0, condition_handle = 0;
  bool expected = false;
  bool operator==(const ProducerOperation &) const = default;
};
[[nodiscard]] ProducerOperation adapt_quantum(const rv32::Decoded &instruction, Id id,
                                              std::uint32_t lhs, std::uint32_t rhs,
                                              std::uint32_t predicate_handle);
struct ControlLinks {
  Mailbox<Group> groups;
  Mailbox<GroupReply> replies;
  Mailbox<EndOfStream> closure;
  Mailbox<Completion> cpu_results, fast_results;
  Mailbox<Token> fast_credits;
  explicit ControlLinks(const Profile &p)
      : groups(1, p.tcu, p.command_latency), replies(1, p.cpu, p.reply_latency),
        closure(1, p.tcu, p.command_latency),
        cpu_results(p.result_slots, p.cpu, p.cpu_result_latency),
        fast_results(p.result_slots, p.tcu, p.fast_result_latency),
        fast_credits(p.result_slots, p.cpu) {}
  void reset();
};
class TimelineProducer {
public:
  using ValidateAction = std::function<void(const ActionSpec &)>;
  TimelineProducer(const Profile &profile, Scoreboard &scoreboard, Trace &trace,
                   ValidateAction validate);
  void receive(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> execute(const ProducerOperation &operation, Tick now, Epoch epoch,
                                       ControlLinks &links);
  void reset();
  [[nodiscard]] Tick cursor() const { return cursor_; }
  [[nodiscard]] bool closed() const { return closed_; }
  [[nodiscard]] bool pending() const { return sealed_.has_value(); }

private:
  bool flush(Tick now, Epoch epoch, ControlLinks &links);
  std::optional<std::uint32_t> append(const ProducerOperation &operation, Tick now, Epoch epoch);
  const Profile &profile_;
  Scoreboard &scoreboard_;
  Trace &trace_;
  ValidateAction validate_;
  std::vector<ReservedEvent> open_events_;
  std::optional<Group> sealed_;
  std::optional<ProducerOperation> held_;
  Tick cursor_ = 0, last_admitted_due_ = 0;
  Id last_label_ = 0, next_event_ = 1;
  bool open_ = false, flushed_ = false, closed_ = false;
};
} // namespace qsbit
