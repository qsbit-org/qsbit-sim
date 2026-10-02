#pragma once

#include "qsbit/cpu.hpp"
#include <map>

namespace qsbit {
struct EqasmMicrocode {
  std::uint32_t opcode = 0;
  std::vector<std::uint32_t> targets;
  std::uint32_t port = 0, codeword = 0;
};
struct EqasmConfiguration {
  std::vector<std::array<std::uint32_t, 2>> qubit_pairs;
  std::vector<EqasmMicrocode> microcode;
};

class EqasmCpuCycleModel final : public ICpuCycleModel {
public:
  EqasmCpuCycleModel(const Profile &profile, std::uint32_t entry, Trace &trace,
                     const EqasmConfiguration &configuration);
  void step(Tick now, Epoch epoch, CpuPorts &ports) override;
  void reset(std::uint32_t entry) override;
  [[nodiscard]] const std::array<std::uint32_t, 32> &registers() const override {
    return registers_;
  }
  [[nodiscard]] std::uint32_t pc() const override { return pc_; }
  [[nodiscard]] bool halted() const override { return halted_; }

private:
  struct Route {
    std::uint32_t port, codeword, qubits;
  };
  struct Operation {
    bool two_qubit = false;
    std::map<std::uint32_t, Route> routes;
  };
  struct Fetch {
    Id id;
    std::uint32_t pc;
  };
  struct Frame {
    Id id = 0;
    std::uint32_t pc = 0, word = 0;
    std::optional<ErrorCode> fault;
    std::vector<ControlOperation> control;
    std::size_t completed = 0;
    std::uint32_t targets = 0;
    bool prepared = false;
  };
  void prepare(Frame &frame);
  bool execute(Frame &frame, Tick now, Epoch epoch, CpuPorts &ports);
  Clock clock_;
  Trace &trace_;
  std::uint32_t qubits_, pair_count_;
  std::map<std::uint32_t, Operation> microcode_;
  std::array<std::uint32_t, 32> registers_{}, single_masks_{}, pair_masks_{};
  std::optional<Fetch> fetch_request_;
  std::optional<Frame> fetched_, decode_, execute_;
  std::uint32_t pc_ = 0, fetch_pc_ = 0, used_targets_ = 0;
  Id next_fetch_ = 1;
  bool halted_ = false;
};
} // namespace qsbit
