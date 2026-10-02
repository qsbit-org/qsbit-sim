#include "qsbit/cpu/eqasm.hpp"
#include <algorithm>
#include <set>

namespace qsbit {
EqasmCpuCycleModel::EqasmCpuCycleModel(const Profile &profile, std::uint32_t entry, Trace &trace,
                                       const EqasmConfiguration &configuration)
    : clock_(profile.cpu), trace_(trace), qubits_(profile.qubits),
      pair_count_(static_cast<std::uint32_t>(configuration.qubit_pairs.size())) {
  profile.validate();
  require(qubits_ <= 7 && configuration.qubit_pairs.size() <= 16, ErrorCode::InvalidProfile,
          "eQASM supports seven qubits and sixteen qubit pairs");
  std::set<std::array<std::uint32_t, 2>> pairs;
  for (const auto &pair : configuration.qubit_pairs)
    require(pair[0] < qubits_ && pair[1] < qubits_ && pair[0] != pair[1] &&
                pairs.insert(pair).second,
            ErrorCode::InvalidProfile, "invalid eQASM qubit pair");
  for (const auto &entry_code : configuration.microcode) {
    require(entry_code.opcode > 0 && entry_code.opcode < 512 &&
                (entry_code.targets.size() == 1 || entry_code.targets.size() == 2),
            ErrorCode::InvalidProfile, "invalid eQASM microcode entry");
    const bool two_qubit = entry_code.targets.size() == 2;
    auto [position, inserted] = microcode_.try_emplace(entry_code.opcode);
    auto &operation = position->second;
    require(inserted || operation.two_qubit == two_qubit, ErrorCode::InvalidProfile,
            "eQASM opcode mixes single-qubit and two-qubit targets");
    operation.two_qubit = two_qubit;
    std::uint32_t target_mask = 0;
    for (auto qubit : entry_code.targets) {
      require(qubit < qubits_, ErrorCode::InvalidProfile, "eQASM target exceeds qubit count");
      target_mask |= 1U << qubit;
    }
    std::uint32_t index = entry_code.targets.front();
    if (two_qubit) {
      const std::array pair{entry_code.targets[0], entry_code.targets[1]};
      const auto found =
          std::find(configuration.qubit_pairs.begin(), configuration.qubit_pairs.end(), pair);
      require(found != configuration.qubit_pairs.end(), ErrorCode::InvalidProfile,
              "eQASM microcode uses an unconfigured qubit pair");
      index = static_cast<std::uint32_t>(found - configuration.qubit_pairs.begin());
    }
    const auto &mapping = profile.mapping(entry_code.port, entry_code.codeword);
    require(std::all_of(mapping.actions.begin(), mapping.actions.end(),
                        [&](const auto &action) { return action.targets == entry_code.targets; }),
            ErrorCode::InvalidProfile, "eQASM microcode and codeword targets differ");
    require(
        operation.routes.emplace(index, Route{entry_code.port, entry_code.codeword, target_mask})
            .second,
        ErrorCode::InvalidProfile, "duplicate eQASM microcode route");
  }
  reset(entry);
}

void EqasmCpuCycleModel::reset(std::uint32_t entry) {
  registers_.fill(0);
  single_masks_.fill(0);
  pair_masks_.fill(0);
  fetch_request_.reset();
  fetched_.reset();
  decode_.reset();
  execute_.reset();
  pc_ = fetch_pc_ = entry;
  used_targets_ = 0;
  next_fetch_ = 1;
  halted_ = false;
}

void EqasmCpuCycleModel::prepare(Frame &frame) {
  const auto word = frame.word;
  if (word >> 31) {
    const auto interval = word & 7U;
    frame.targets = interval ? 0 : used_targets_;
    if (interval)
      frame.control.push_back({frame.id, ControlKind::Wait, interval});
    for (const auto shift : {17U, 3U}) {
      const auto slot = (word >> shift) & 0x3fffU;
      const auto opcode = slot >> 5;
      if (opcode == 0) {
        require((slot & 31U) == 0, ErrorCode::IllegalInstruction, "invalid QNOP operand");
        continue;
      }
      const auto found = microcode_.find(opcode);
      require(found != microcode_.end(), ErrorCode::IllegalInstruction,
              "unconfigured eQASM quantum opcode");
      const auto &operation = found->second;
      const auto mask = (operation.two_qubit ? pair_masks_ : single_masks_)[slot & 31U];
      for (std::uint32_t bit = 0; bit < (operation.two_qubit ? 16U : 7U); ++bit) {
        if (!(mask & (1U << bit)))
          continue;
        const auto route = operation.routes.find(bit);
        require(route != operation.routes.end(), ErrorCode::InvalidOperand,
                "eQASM target has no microcode route");
        require(!(frame.targets & route->second.qubits), ErrorCode::ResourceConflict,
                "multiple eQASM operations target the same qubit at one time point");
        frame.targets |= route->second.qubits;
        frame.control.push_back(
            {frame.id, ControlKind::Codeword, route->second.port, route->second.codeword});
      }
    }
  } else {
    const auto opcode = word >> 25;
    const auto reserved = [&](std::uint32_t mask) {
      require((word & mask) == 0, ErrorCode::IllegalInstruction,
              "nonzero reserved eQASM instruction bits");
    };
    switch (opcode) {
    case 0x00:
    case 0x08:
      reserved(0x1ffffff);
      if (opcode == 0x08)
        frame.control.push_back({frame.id, ControlKind::Halt});
      break;
    case 0x15:
      reserved(0xffff8);
      require((word & 7U) < qubits_, ErrorCode::InvalidOperand,
              "invalid eQASM measurement register");
      frame.control.push_back({frame.id, ControlKind::FetchMeasurement, word & 7U});
      break;
    case 0x16:
    case 0x17:
      break;
    case 0x20:
      reserved(0xfff80);
      require((word & 127U) < (1U << qubits_), ErrorCode::InvalidOperand,
              "SMIS mask exceeds qubit count");
      break;
    case 0x28:
      reserved(0xf0000);
      require((word & 65535U) < (1U << pair_count_), ErrorCode::InvalidOperand,
              "SMIT mask exceeds configured qubit pairs");
      break;
    case 0x30:
      reserved(0x1f00000);
      frame.control.push_back({frame.id, ControlKind::Wait, word & 0xfffffU});
      break;
    case 0x38:
      reserved(0x1f07fff);
      frame.control.push_back(
          {frame.id, ControlKind::Wait, registers_[(word >> 15) & 31U] & 0xfffffU});
      break;
    default:
      throw Fault(ErrorCode::IllegalInstruction, "unsupported eQASM instruction");
    }
  }
  frame.prepared = true;
}

bool EqasmCpuCycleModel::execute(Frame &frame, Tick now, Epoch epoch, CpuPorts &ports) {
  if (frame.fault)
    throw Fault(*frame.fault, "eQASM instruction fetch fault");
  if (!frame.prepared)
    prepare(frame);
  std::uint32_t value = 0;
  while (frame.completed < frame.control.size()) {
    const auto &operation = frame.control[frame.completed];
    const auto reply = ports.control(operation);
    if (!reply)
      return false;
    value = *reply;
    if (operation.kind == ControlKind::Wait && operation.first)
      used_targets_ = 0;
    ++frame.completed;
  }
  const auto word = frame.word;
  const auto rd = (word >> 20) & 31U;
  bool writes_register = false;
  if (word >> 31) {
    used_targets_ = frame.targets;
  } else {
    switch (word >> 25) {
    case 0x08:
      halted_ = true;
      break;
    case 0x15:
      writes_register = true;
      break;
    case 0x16:
      value = ((word & 0xfffffU) ^ 0x80000U) - 0x80000U;
      writes_register = true;
      break;
    case 0x17:
      value = ((word & 0x7fffU) << 17) | (registers_[(word >> 15) & 31U] & 0x1ffffU);
      writes_register = true;
      break;
    case 0x20:
      single_masks_[rd] = word & 127U;
      break;
    case 0x28:
      pair_masks_[rd] = word & 65535U;
      break;
    default:
      break;
    }
  }
  if (writes_register)
    registers_[rd] = value;
  pc_ = halted_ ? frame.pc : frame.pc + 4;
  TraceEvent event{now,      epoch, "InstructionRetired",
                   frame.id, 0,     (now - clock_.phase) / clock_.period};
  event.pc = frame.pc;
  event.word = word;
  event.next_pc = pc_;
  event.rd = writes_register ? rd : 0;
  event.value = value;
  event.registers.assign(registers_.begin(), registers_.end());
  trace_.emit(std::move(event));
  return true;
}

void EqasmCpuCycleModel::step(Tick now, Epoch epoch, CpuPorts &ports) {
  require(clock_.edge(now), ErrorCode::Protocol, "eQASM CPU invoked off-edge");
  if (auto response = ports.fetch.responses.take(now)) {
    if (response->epoch == epoch) {
      require(fetch_request_ && response->value.id == fetch_request_->id, ErrorCode::Protocol,
              "unexpected eQASM fetch response");
      if (!halted_) {
        require(!fetched_, ErrorCode::Protocol, "eQASM fetch latch overflow");
        Frame frame;
        frame.id = fetch_request_->id;
        frame.pc = fetch_request_->pc;
        frame.word = response->value.value;
        frame.fault = response->value.fault;
        fetched_ = std::move(frame);
      }
      fetch_request_.reset();
    }
  }
  if (halted_)
    return;
  if (execute_) {
    if (execute(*execute_, now, epoch, ports))
      execute_.reset();
    else {
      TraceEvent event{now, epoch, "CpuStalled", execute_->id};
      event.detail = "timing control or measurement register";
      trace_.emit(std::move(event));
    }
  }
  if (halted_) {
    decode_.reset();
    fetched_.reset();
    return;
  }
  if (!execute_ && decode_) {
    execute_ = std::move(decode_);
    decode_.reset();
  }
  if (!decode_ && fetched_) {
    decode_ = std::move(fetched_);
    fetched_.reset();
  }
  if (!fetch_request_ && !fetched_ && !ports.fetch.requests.full()) {
    const auto id = next_fetch_;
    next_fetch_ = checked_add(next_fetch_, 1);
    ports.fetch.requests.publish(now, epoch, MemoryRequest{id, fetch_pc_, 0, 4, false, true});
    fetch_request_ = Fetch{id, fetch_pc_};
    fetch_pc_ += 4;
  }
}
} // namespace qsbit
