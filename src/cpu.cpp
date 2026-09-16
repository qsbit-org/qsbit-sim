#include "qsbit/cpu.hpp"
#include <utility>

namespace qsbit {
CpuCycleModel::CpuCycleModel(Clock clock, std::uint32_t entry, Trace &trace)
    : clock_(clock), trace_(trace) {
  reset(entry);
}
void CpuCycleModel::reset(std::uint32_t entry) {
  registers_.fill(0);
  pc_ = fetch_pc_ = entry;
  fetch_request_.reset();
  fetched_.reset();
  decode_.reset();
  execute_.reset();
  next_fetch_ = next_data_ = 1;
  generation_ = 0;
  halted_ = false;
}
void CpuCycleModel::retire(const Frame &frame, std::uint32_t value, std::uint32_t next_pc, Tick now,
                           Epoch epoch) {
  if (frame.decoded->writes_rd && frame.decoded->rd != 0)
    registers_[frame.decoded->rd] = value;
  registers_[0] = 0;
  pc_ = next_pc;
  TraceEvent event{now,      epoch, "InstructionRetired",
                   frame.id, 0,     (now - clock_.phase) / clock_.period};
  event.value = value;
  event.pc = frame.pc;
  event.word = frame.word;
  event.rd = frame.decoded->writes_rd ? frame.decoded->rd : 0;
  event.next_pc = next_pc;
  event.registers.assign(registers_.begin(), registers_.end());
  trace_.emit(std::move(event));
}
void CpuCycleModel::step(Tick now, Epoch epoch, CpuPorts &ports) {
  require(clock_.edge(now), ErrorCode::Protocol, "CPU invoked off-edge");
  if (auto response = ports.fetch.responses.take(now)) {
    if (response->epoch == epoch) {
      require(fetch_request_ && response->value.id == fetch_request_->id, ErrorCode::Protocol,
              "unexpected fetch response");
      if (fetch_request_->generation == generation_ && !halted_) {
        require(!fetched_, ErrorCode::Protocol, "fetch latch overflow");
        Frame frame;
        frame.id = fetch_request_->id;
        frame.pc = fetch_request_->pc;
        frame.word = response->value.value;
        frame.fault = response->value.fault;
        fetched_ = frame;
      }
      fetch_request_.reset();
    }
  }
  if (halted_)
    return;
  bool flush = false;
  if (execute_) {
    auto &frame = *execute_;
    if (frame.fault)
      throw Fault(*frame.fault, "oldest instruction fault at PC " + std::to_string(frame.pc));
    require(frame.decoded.has_value(), ErrorCode::Protocol, "execute stage has no decode");
    const auto &d = *frame.decoded;
    bool completed = false;
    std::uint32_t value = 0, next_pc = frame.pc + 4;
    if (d.op == rv32::Op::Quantum) {
      auto reply = ports.control(adapt_quantum(d, frame.id, frame.lhs, frame.rhs, frame.predicate));
      if (reply) {
        value = *reply;
        completed = true;
        if (((d.word >> 12) & 7) == 4) {
          halted_ = true;
          flush = true;
        }
      }
    } else {
      if (!frame.effect)
        frame.effect = rv32::evaluate(d, frame.pc, frame.lhs, frame.rhs);
      const auto &effect = *frame.effect;
      value = effect.value;
      next_pc = effect.next_pc;
      if (effect.memory == rv32::MemoryKind::None) {
        completed = true;
        flush = effect.branch_taken;
      } else if (frame.memory_request == 0) {
        if (!ports.data.requests.full()) {
          const auto id = next_data_;
          next_data_ = checked_add(next_data_, 1);
          ports.data.requests.publish(
              now, epoch,
              MemoryRequest{id, effect.address, effect.store_value, effect.width,
                            effect.memory == rv32::MemoryKind::Store, false});
          frame.memory_request = id;
        }
      } else if (auto response = ports.data.responses.take(now)) {
        require(response->epoch == epoch && response->value.id == frame.memory_request,
                ErrorCode::Protocol, "unexpected data response");
        if (response->value.fault)
          throw Fault(*response->value.fault, "oldest data access fault");
        value = response->value.value;
        if (effect.sign_extend_load)
          value = rv32::sign_extend(value, effect.width * 8);
        completed = true;
      }
    }
    if (completed) {
      retire(frame, value, next_pc, now, epoch);
      execute_.reset();
    } else {
      TraceEvent event{now, epoch, "CpuStalled", frame.id};
      event.detail = d.op == rv32::Op::Quantum ? "producer or measurement" : "memory response";
      trace_.emit(std::move(event));
    }
  }
  if (flush) {
    decode_.reset();
    fetched_.reset();
    generation_ = checked_add(generation_, 1);
    fetch_pc_ = pc_;
    trace_.emit({now, epoch, "PipelineFlushed"});
  }
  if (halted_)
    return;
  if (!execute_ && decode_) {
    Frame frame = std::move(*decode_);
    decode_.reset();
    if (!frame.fault) {
      try {
        frame.decoded = rv32::decode(frame.word);
        const auto &d = *frame.decoded;
        frame.lhs = registers_[d.rs1];
        frame.rhs = registers_[d.rs2];
        frame.predicate = registers_[d.rd];
      } catch (const Fault &fault) {
        frame.fault = fault.code();
      }
    }
    execute_ = std::move(frame);
  }
  if (!decode_ && fetched_) {
    decode_ = std::move(fetched_);
    fetched_.reset();
  }
  if (!fetch_request_ && !fetched_ && !ports.fetch.requests.full()) {
    const auto id = next_fetch_;
    next_fetch_ = checked_add(next_fetch_, 1);
    ports.fetch.requests.publish(now, epoch, MemoryRequest{id, fetch_pc_, 0, 4, false, true});
    fetch_request_ = Fetch{id, fetch_pc_, generation_};
    fetch_pc_ += 4;
  }
}
} // namespace qsbit
