#include "qsbit/timing_control.hpp"
#include <algorithm>
#include <utility>

namespace qsbit {
ControlOperation adapt_quantum(const rv32::Decoded &d, Id id, std::uint32_t lhs,
                               std::uint32_t rhs) {
  require(d.op == rv32::Op::Quantum, ErrorCode::Protocol, "non-quantum operation sent to adapter");
  ControlOperation op;
  op.instruction = id;
  op.first = lhs;
  op.second = rhs;
  switch ((d.word >> 12) & 7) {
  case 0:
    op.kind = ControlKind::Codeword;
    op.first = (d.word >> 25) & 1 ? d.rs1 : lhs;
    op.second = (d.word >> 25) & 2 ? d.rs2 : rhs;
    break;
  case 1:
    op.kind = ControlKind::Wait;
    break;
  case 2:
    op.kind = ControlKind::Wait;
    op.first = d.word >> 15;
    break;
  case 3:
    op.kind = ControlKind::FetchMeasurement;
    op.first = d.rs1;
    break;
  case 6:
    op.kind = ControlKind::Synchronize;
    op.first = d.word >> 15;
    break;
  default:
    throw Fault(ErrorCode::IllegalInstruction, "reserved quantum operation");
  }
  return op;
}
void ControlLinks::reset() {
  timing_events.reset();
  replies.reset();
  closure.reset();
  cpu_results.reset();
  fast_results.reset();
  fast_credits.reset();
}
TimingControl::TimingControl(const Profile &profile, MeasurementRegisters &registers, Trace &trace,
                             ValidateAction validate, ValidateSync validate_sync)
    : profile_(profile), measurement_registers_(registers), trace_(trace),
      validate_(std::move(validate)), validate_sync_(std::move(validate_sync)) {}
void TimingControl::receive(Tick now, Epoch epoch, ControlLinks &links) {
  if (auto reply = links.replies.take(now)) {
    if (reply->epoch == epoch) {
      require(enqueue_request_ && reply->value.label == enqueue_request_->point.label,
              ErrorCode::Protocol, "unexpected request acknowledgment");
      last_label_ = reply->value.label;
      last_enqueued_time_ = time_point_;
      enqueue_request_.reset();
      pending_events_.clear();
      pending_sync_.clear();
      pending_point_ = false;
      enqueued_ = true;
      trace_.emit({now, epoch, "EnqueueAcknowledged", 0, last_label_});
    }
  }
  while (auto reply = links.cpu_results.take(now)) {
    if (reply->epoch != epoch) {
      trace_.emit({now, epoch, "StaleCompletionDiscarded"});
      continue;
    }
    measurement_registers_.deliver(reply->value, epoch);
    TraceEvent e{now, epoch, "MeasurementRegisterUpdated", reply->value.reference.measurement};
    e.targets = {reply->value.reference.target};
    e.value = reply->value.value;
    trace_.emit(std::move(e));
  }
  while (auto credit = links.fast_credits.take(now)) {
    if (credit->epoch == epoch)
      measurement_registers_.acknowledge_fast(credit->value, epoch);
  }
}
bool TimingControl::enqueue(Tick now, Epoch epoch, ControlLinks &links) {
  if (enqueue_request_)
    return false;
  if (!pending_point_) {
    enqueued_ = !allow_same_time_;
    return true;
  }
  TimingEvents request;
  request.point = {epoch, checked_add(last_label_, 1), time_point_ - last_enqueued_time_, {}};
  request.events = pending_events_;
  request.point.synchronizations = pending_sync_;
  request.point.wait_for_next = pending_wait_for_next_;
  request.configuration = profile_.fingerprint();
  for (auto &event : request.events) {
    event.label = request.point.label;
    request.point.manifest.push_back(event.id);
  }
  validate_timing_events(request, profile_);
  require(!links.timing_events.full(), ErrorCode::Protocol, "enqueue request already pending");
  enqueue_request_ = request;
  links.timing_events.publish(now, epoch, std::move(request));
  trace_.emit({now, epoch, "TimingPointSubmitted", 0, enqueue_request_->point.label, time_point_});
  return false;
}
std::optional<std::uint32_t> TimingControl::execute_zero_wait(Tick now, Epoch epoch,
                                                              ControlLinks &links) {
  if (!pending_wait_for_next_) {
    pending_wait_for_next_ = true;
    pending_point_ = true;
  }
  if (!enqueue(now, epoch, links))
    return std::nullopt;
  pending_wait_for_next_ = false;
  allow_same_time_ = true;
  enqueued_ = false;
  return 0;
}
std::optional<std::uint32_t> TimingControl::advance_time(Tick interval, Tick now, Epoch epoch,
                                                         ControlLinks &links) {
  const auto destination = checked_add(time_point_, interval);
  if (!enqueue(now, epoch, links))
    return std::nullopt;
  time_point_ = destination;
  allow_same_time_ = false;
  pending_point_ = true;
  enqueued_ = false;
  return 0;
}
std::optional<std::uint32_t> TimingControl::codeword(const ControlOperation &operation, Tick now,
                                                     Epoch epoch) {
  require(!enqueued_ && !enqueue_request_, ErrorCode::Protocol,
          "cw requires wait after a submitted time point");
  const auto &map = profile_.mapping(operation.first, operation.second);
  std::optional<std::uint32_t> measurement_target;
  for (const auto &action : map.actions) {
    validate_(action);
    require(action.execution_flag() == ExecutionFlag::Always || profile_.fast_feedback,
            ErrorCode::UnsupportedCapability, "fast feedback is disabled");
    if (action.kind() == ActionKind::Acquire)
      measurement_target = action.targets().front();
  }
  const auto count = checked_add(pending_events_.size(), map.actions.size());
  require(count <= profile_.staging_capacity, ErrorCode::Capacity,
          "cw exceeds timing-point event capacity");
  std::vector<std::uint32_t> counts(profile_.ports, 0);
  for (const auto &e : pending_events_)
    ++counts.at(e.action.port);
  for (const auto &a : map.actions) {
    ++counts.at(a.port);
    require(counts[a.port] <= profile_.firing_width && counts[a.port] <= profile_.event_capacity,
            ErrorCode::Capacity, "cw exceeds per-port event capacity");
  }
  const auto next_event = checked_add(next_event_, map.actions.size());
  std::optional<MeasurementReference> reference;
  if (measurement_target) {
    require(measurement_registers_.has_capacity(), ErrorCode::Capacity,
            "measurement delivery capacity exhausted");
    reference = measurement_registers_.reserve(epoch, *measurement_target);
  }
  auto events = decode_codeword(profile_, operation.first, operation.second, epoch,
                                operation.instruction, next_event_, reference);
  pending_events_.insert(pending_events_.end(), events.begin(), events.end());
  pending_point_ = true;
  next_event_ = next_event;
  TraceEvent record{now, epoch, "CodewordQueued", operation.instruction, 0, time_point_};
  record.port = operation.first;
  record.codeword = operation.second;
  trace_.emit(std::move(record));
  return 0;
}
std::optional<std::uint32_t> TimingControl::execute(const ControlOperation &operation, Tick now,
                                                    Epoch epoch, ControlLinks &links) {
  require(!closed_, ErrorCode::Protocol, "control operation after program exit");
  if (held_)
    require(*held_ == operation, ErrorCode::Protocol,
            "blocked instruction changed identity or operands");
  else
    held_ = operation;
  std::optional<std::uint32_t> result;
  switch (operation.kind) {
  case ControlKind::Codeword:
    result = codeword(operation, now, epoch);
    break;
  case ControlKind::Wait:
    result = operation.first == 0 ? execute_zero_wait(now, epoch, links)
                                  : advance_time(operation.first, now, epoch, links);
    break;
  case ControlKind::FetchMeasurement:
    require(operation.first < profile_.qubits, ErrorCode::InvalidOperand,
            "measurement register is invalid");
    if (enqueue(now, epoch, links)) {
      const auto value = measurement_registers_.read(operation.first);
      if (value) {
        result = *value ? 1U : 0U;
        TraceEvent e{now, epoch, "MeasurementRegisterRead", operation.instruction};
        e.targets = {operation.first};
        e.value = *result;
        trace_.emit(std::move(e));
      }
    }
    break;
  case ControlKind::Halt:
    if (enqueue(now, epoch, links)) {
      links.closure.publish(now, epoch, EndOfStream{last_label_});
      closed_ = true;
      result = 0;
    }
    break;
  case ControlKind::Synchronize:
    require(bool(validate_sync_), ErrorCode::UnsupportedSynchronization,
            "sync requires a connected controller");
    require(!enqueued_ && !enqueue_request_, ErrorCode::Protocol,
            "sync requires wait after a submitted time point");
    validate_sync_(operation.first);
    require(pending_sync_.empty(), ErrorCode::InvalidOperand,
            "only one sync is allowed at a time point");
    pending_sync_.push_back(operation.first);
    pending_point_ = true;
    result = 0;
    break;
  }
  if (result)
    held_.reset();
  return result;
}
void TimingControl::reset() {
  pending_events_.clear();
  pending_sync_.clear();
  enqueue_request_.reset();
  held_.reset();
  time_point_ = 0;
  last_enqueued_time_ = 0;
  last_label_ = 0;
  next_event_ = 1;
  pending_point_ = false;
  enqueued_ = false;
  closed_ = false;
  pending_wait_for_next_ = false;
  allow_same_time_ = false;
}
} // namespace qsbit
