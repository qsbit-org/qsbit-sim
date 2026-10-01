#include "qsbit/producer.hpp"
#include <algorithm>
#include <utility>

namespace qsbit {
ControlOperation adapt_quantum(const rv32::Decoded &d, Id id, std::uint32_t lhs, std::uint32_t rhs,
                               std::uint32_t predicate_handle) {
  require(d.op == rv32::Op::Quantum, ErrorCode::Protocol, "non-quantum operation sent to adapter");
  ControlOperation op;
  op.instruction = id;
  op.first = lhs;
  op.second = rhs;
  switch ((d.word >> 12) & 7) {
  case 0:
    op.kind = ControlKind::Append;
    break;
  case 1:
    op.kind = ControlKind::Advance;
    break;
  case 2:
    op.kind = ControlKind::Flush;
    break;
  case 3:
    op.kind = ControlKind::ReadResult;
    break;
  case 4:
    op.kind = ControlKind::End;
    break;
  case 5:
    op.kind = ControlKind::ConditionalAppend;
    op.condition_handle = predicate_handle;
    op.expected = (d.word >> 25) != 0;
    break;
  case 6:
    op.kind = ControlKind::Synchronize;
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
TimingControl::TimingControl(const Profile &profile, MeasurementResults &scoreboard, Trace &trace,
                             ValidateAction validate)
    : profile_(profile), measurement_results_(scoreboard), trace_(trace),
      validate_(std::move(validate)) {}
void TimingControl::receive(Tick now, Epoch epoch, ControlLinks &links) {
  if (auto reply = links.replies.take(now)) {
    if (reply->epoch == epoch) {
      require(enqueue_request_ && reply->value.label == enqueue_request_->point.label,
              ErrorCode::Protocol, "unexpected group acknowledgment");
      last_label_ = reply->value.label;
      last_enqueued_time_ = time_point_;
      enqueue_request_.reset();
      pending_events_.clear();
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
    measurement_results_.deliver(reply->value, epoch);
    TraceEvent e{now, epoch, "CpuResultVisible", reply->value.token.measurement};
    e.targets = {reply->value.token.target};
    e.value = reply->value.value;
    trace_.emit(std::move(e));
  }
  while (auto credit = links.fast_credits.take(now)) {
    if (credit->epoch == epoch)
      measurement_results_.acknowledge_fast(credit->value, epoch);
  }
}
bool TimingControl::flush(Tick now, Epoch epoch, ControlLinks &links) {
  if (enqueue_request_)
    return false;
  if (!open_) {
    flushed_ = true;
    return true;
  }
  TimingEvents group;
  group.point = {epoch, checked_add(last_label_, 1), time_point_ - last_enqueued_time_, {}};
  group.events = pending_events_;
  group.configuration = profile_.fingerprint();
  for (auto &event : group.events) {
    event.label = group.point.label;
    group.point.manifest.push_back(event.id);
  }
  validate_timing_events(group, profile_);
  require(!links.groups.full(), ErrorCode::Protocol, "more than one frozen group submission");
  enqueue_request_ = group;
  links.groups.publish(now, epoch, std::move(group));
  trace_.emit({now, epoch, "GroupSubmitted", 0, enqueue_request_->point.label, time_point_});
  return false;
}
std::optional<std::uint32_t> TimingControl::append(const ControlOperation &operation, Tick now,
                                                   Epoch epoch) {
  require(!flushed_ && !enqueue_request_, ErrorCode::Protocol,
          "APPEND requires a positive ADVANCE after FLUSH");
  const auto &map = profile_.mapping(operation.first, operation.second);
  std::optional<Condition> condition;
  if (operation.kind == ControlKind::ConditionalAppend)
    condition = Condition{measurement_results_.token(operation.condition_handle, epoch),
                          operation.expected};
  std::optional<std::uint32_t> measurement_target;
  for (const auto &action : map.actions) {
    validate_(action);
    if (action.kind == ActionKind::Acquire)
      measurement_target = action.targets.front();
  }
  require(!(measurement_target && condition), ErrorCode::UnsupportedCapability,
          "conditional measurement is unsupported");
  const auto count = checked_add(pending_events_.size(), map.actions.size());
  require(count <= profile_.staging_capacity, ErrorCode::Capacity,
          "APPEND exceeds bounded staging");
  std::vector<std::uint32_t> counts(profile_.ports, 0);
  for (const auto &e : pending_events_)
    ++counts.at(e.action.port);
  for (const auto &a : map.actions) {
    ++counts.at(a.port);
    require(counts[a.port] <= profile_.firing_width && counts[a.port] <= profile_.event_capacity,
            ErrorCode::Capacity, "APPEND creates an impossible per-port group");
  }
  const auto next_event = checked_add(next_event_, map.actions.size());
  std::optional<MeasurementReference> token;
  if (measurement_target) {
    // Only a future CPU READ can release a full CPU slot; stalling this APPEND would deadlock.
    require(measurement_results_.has_slot(), ErrorCode::Capacity,
            "no CPU result slot; consume a prior result first");
    if (!measurement_results_.has_fast_credit())
      return std::nullopt;
    token = measurement_results_.reserve(epoch, *measurement_target);
  }
  auto events = decode_codeword(profile_, operation.first, operation.second, epoch,
                                operation.instruction, next_event_, token, condition);
  pending_events_.insert(pending_events_.end(), events.begin(), events.end());
  open_ = true;
  next_event_ = next_event;
  TraceEvent record{now, epoch, "ProducerAccepted", operation.instruction, 0, time_point_};
  record.port = operation.first;
  record.codeword = operation.second;
  record.value = token ? token->handle : 0;
  trace_.emit(std::move(record));
  return token ? token->handle : 0;
}
std::optional<std::uint32_t> TimingControl::execute(const ControlOperation &operation, Tick now,
                                                    Epoch epoch, ControlLinks &links) {
  require(!closed_, ErrorCode::Protocol, "producer operation after END");
  if (held_)
    require(*held_ == operation, ErrorCode::Protocol,
            "blocked instruction changed identity or operands");
  else
    held_ = operation;
  std::optional<std::uint32_t> result;
  switch (operation.kind) {
  case ControlKind::Append:
  case ControlKind::ConditionalAppend:
    result = append(operation, now, epoch);
    break;
  case ControlKind::Advance:
    if (operation.first == 0) {
      result = 0;
      break;
    }
    {
      const auto destination = checked_add(time_point_, operation.first);
      if (!flush(now, epoch, links))
        break;
      time_point_ = destination;
      open_ = true;
      flushed_ = false;
      result = 0;
    }
    break;
  case ControlKind::Flush:
    if (flush(now, epoch, links))
      result = 0;
    break;
  case ControlKind::ReadResult:
    (void)measurement_results_.token(operation.first, epoch);
    if (flush(now, epoch, links)) {
      const auto value = measurement_results_.consume(operation.first, epoch);
      if (value) {
        result = *value ? 1U : 0U;
        TraceEvent e{now, epoch, "ResultConsumed", operation.instruction};
        e.value = *result;
        trace_.emit(std::move(e));
      }
    }
    break;
  case ControlKind::End:
    if (flush(now, epoch, links)) {
      links.closure.publish(now, epoch, EndOfStream{last_label_});
      closed_ = true;
      result = 0;
    }
    break;
  case ControlKind::Synchronize:
    throw Fault(ErrorCode::UnsupportedSynchronization, "QSYNC is not enabled in v1");
  }
  if (result)
    held_.reset();
  return result;
}
void TimingControl::reset() {
  pending_events_.clear();
  enqueue_request_.reset();
  held_.reset();
  time_point_ = 0;
  last_enqueued_time_ = 0;
  last_label_ = 0;
  next_event_ = 1;
  open_ = false;
  flushed_ = false;
  closed_ = false;
}
} // namespace qsbit
