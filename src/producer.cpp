#include "qsbit/producer.hpp"
#include <algorithm>
#include <utility>

namespace qsbit {
ProducerOperation adapt_quantum(const rv32::Decoded &d, Id id, std::uint32_t lhs, std::uint32_t rhs,
                                std::uint32_t predicate_handle) {
  require(d.op == rv32::Op::Quantum, ErrorCode::Protocol, "non-quantum operation sent to adapter");
  ProducerOperation op;
  op.instruction = id;
  op.first = lhs;
  op.second = rhs;
  switch ((d.word >> 12) & 7) {
  case 0:
    op.kind = ProducerKind::Append;
    break;
  case 1:
    op.kind = ProducerKind::Advance;
    break;
  case 2:
    op.kind = ProducerKind::Flush;
    break;
  case 3:
    op.kind = ProducerKind::ReadResult;
    break;
  case 4:
    op.kind = ProducerKind::End;
    break;
  case 5:
    op.kind = ProducerKind::ConditionalAppend;
    op.condition_handle = predicate_handle;
    op.expected = (d.word >> 25) != 0;
    break;
  case 6:
    op.kind = ProducerKind::Synchronize;
    break;
  default:
    throw Fault(ErrorCode::IllegalInstruction, "reserved quantum operation");
  }
  return op;
}
void ControlLinks::reset() {
  groups.reset();
  replies.reset();
  closure.reset();
  cpu_results.reset();
  fast_results.reset();
  fast_credits.reset();
}
TimelineProducer::TimelineProducer(const Profile &profile, Scoreboard &scoreboard, Trace &trace,
                                   ValidateAction validate)
    : profile_(profile), scoreboard_(scoreboard), trace_(trace), validate_(std::move(validate)) {}
void TimelineProducer::receive(Tick now, Epoch epoch, ControlLinks &links) {
  if (auto reply = links.replies.take(now)) {
    if (reply->epoch == epoch) {
      require(sealed_ && reply->value.label == sealed_->point.label, ErrorCode::Protocol,
              "unexpected group acknowledgment");
      last_label_ = reply->value.label;
      last_admitted_due_ = cursor_;
      sealed_.reset();
      open_events_.clear();
      open_ = false;
      flushed_ = true;
      trace_.emit({now, epoch, "GroupReplyVisible", 0, last_label_});
    }
  }
  while (auto reply = links.cpu_results.take(now)) {
    if (reply->epoch != epoch) {
      trace_.emit({now, epoch, "StaleCompletionDiscarded"});
      continue;
    }
    scoreboard_.deliver(reply->value, epoch);
    TraceEvent e{now, epoch, "CpuResultVisible", reply->value.token.measurement};
    e.targets = {reply->value.token.target};
    e.value = reply->value.value;
    trace_.emit(std::move(e));
  }
  while (auto credit = links.fast_credits.take(now)) {
    if (credit->epoch == epoch)
      scoreboard_.acknowledge_fast(credit->value, epoch);
  }
}
bool TimelineProducer::flush(Tick now, Epoch epoch, ControlLinks &links) {
  if (sealed_)
    return false;
  if (!open_) {
    flushed_ = true;
    return true;
  }
  Group group;
  group.point = {epoch, checked_add(last_label_, 1), cursor_ - last_admitted_due_, {}};
  group.events = open_events_;
  group.configuration = profile_.fingerprint();
  for (auto &event : group.events) {
    event.label = group.point.label;
    group.point.manifest.push_back(event.id);
  }
  validate_group(group, profile_);
  require(!links.groups.full(), ErrorCode::Protocol, "more than one frozen group submission");
  sealed_ = group;
  links.groups.publish(now, epoch, std::move(group));
  trace_.emit({now, epoch, "GroupSubmitted", 0, sealed_->point.label, cursor_});
  return false;
}
std::optional<std::uint32_t> TimelineProducer::append(const ProducerOperation &operation, Tick now,
                                                      Epoch epoch) {
  require(!flushed_ && !sealed_, ErrorCode::Protocol,
          "APPEND requires a positive ADVANCE after FLUSH");
  const auto &map = profile_.mapping(operation.first, operation.second);
  std::optional<Condition> condition;
  if (operation.kind == ProducerKind::ConditionalAppend)
    condition = Condition{scoreboard_.token(operation.condition_handle, epoch), operation.expected};
  std::optional<std::uint32_t> measurement_target;
  for (const auto &action : map.actions) {
    validate_(action);
    if (action.kind == ActionKind::Acquire)
      measurement_target = action.targets.front();
  }
  require(!(measurement_target && condition), ErrorCode::UnsupportedCapability,
          "conditional measurement is unsupported");
  const auto count = checked_add(open_events_.size(), map.actions.size());
  require(count <= profile_.staging_capacity, ErrorCode::Capacity,
          "APPEND exceeds bounded staging");
  std::vector<std::uint32_t> counts(profile_.ports, 0);
  for (const auto &e : open_events_)
    ++counts.at(e.action.port);
  for (const auto &a : map.actions) {
    ++counts.at(a.port);
    require(counts[a.port] <= profile_.firing_width && counts[a.port] <= profile_.event_capacity,
            ErrorCode::Capacity, "APPEND creates an impossible per-port group");
  }
  const auto next_event = checked_add(next_event_, map.actions.size());
  std::optional<Token> token;
  if (measurement_target) {
    // Only a future CPU READ can release a full CPU slot; stalling this APPEND would deadlock.
    require(scoreboard_.has_slot(), ErrorCode::Capacity,
            "no CPU result slot; consume a prior result first");
    if (!scoreboard_.has_fast_credit())
      return std::nullopt;
    token = scoreboard_.reserve(epoch, *measurement_target);
  }
  auto events = lower(profile_, operation.first, operation.second, epoch, operation.instruction,
                      next_event_, token, condition);
  open_events_.insert(open_events_.end(), events.begin(), events.end());
  open_ = true;
  next_event_ = next_event;
  TraceEvent record{now, epoch, "ProducerAccepted", operation.instruction, 0, cursor_};
  record.port = operation.first;
  record.codeword = operation.second;
  record.value = token ? token->handle : 0;
  trace_.emit(std::move(record));
  return token ? token->handle : 0;
}
std::optional<std::uint32_t> TimelineProducer::execute(const ProducerOperation &operation, Tick now,
                                                       Epoch epoch, ControlLinks &links) {
  require(!closed_, ErrorCode::Protocol, "producer operation after END");
  if (held_)
    require(*held_ == operation, ErrorCode::Protocol,
            "blocked instruction changed identity or operands");
  else
    held_ = operation;
  std::optional<std::uint32_t> result;
  switch (operation.kind) {
  case ProducerKind::Append:
  case ProducerKind::ConditionalAppend:
    result = append(operation, now, epoch);
    break;
  case ProducerKind::Advance:
    if (operation.first == 0) {
      result = 0;
      break;
    }
    {
      const auto destination = checked_add(cursor_, operation.first);
      if (!flush(now, epoch, links))
        break;
      cursor_ = destination;
      open_ = true;
      flushed_ = false;
      result = 0;
    }
    break;
  case ProducerKind::Flush:
    if (flush(now, epoch, links))
      result = 0;
    break;
  case ProducerKind::ReadResult:
    (void)scoreboard_.token(operation.first, epoch);
    if (flush(now, epoch, links)) {
      const auto value = scoreboard_.consume(operation.first, epoch);
      if (value) {
        result = *value ? 1U : 0U;
        TraceEvent e{now, epoch, "ResultConsumed", operation.instruction};
        e.value = *result;
        trace_.emit(std::move(e));
      }
    }
    break;
  case ProducerKind::End:
    if (flush(now, epoch, links)) {
      links.closure.publish(now, epoch, EndOfStream{last_label_});
      closed_ = true;
      result = 0;
    }
    break;
  case ProducerKind::Synchronize:
    throw Fault(ErrorCode::UnsupportedSynchronization, "QSYNC is not enabled in v1");
  }
  if (result)
    held_.reset();
  return result;
}
void TimelineProducer::reset() {
  open_events_.clear();
  sealed_.reset();
  held_.reset();
  cursor_ = 0;
  last_admitted_due_ = 0;
  last_label_ = 0;
  next_event_ = 1;
  open_ = false;
  flushed_ = false;
  closed_ = false;
}
} // namespace qsbit
