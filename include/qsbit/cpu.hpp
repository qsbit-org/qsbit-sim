#pragma once

#include "qsbit/isa.hpp"
#include "qsbit/memory.hpp"
#include "qsbit/producer.hpp"
#include "qsbit/trace.hpp"
#include <array>
#include <functional>

namespace qsbit {
struct CpuPorts {
  MemoryPort &fetch;
  MemoryPort &data;
  std::function<std::optional<std::uint32_t>(const ProducerOperation &)> control;
};
// Adapter contract intentionally contains no SystemC types or concrete pipeline latches.
class ICpuCycleModel {
public:
  virtual ~ICpuCycleModel() = default;
  virtual void step(Tick now, Epoch epoch, CpuPorts &ports) = 0;
  virtual void reset(std::uint32_t entry) = 0;
  [[nodiscard]] virtual const std::array<std::uint32_t, 32> &registers() const = 0;
  [[nodiscard]] virtual std::uint32_t pc() const = 0;
  [[nodiscard]] virtual bool halted() const = 0;
};
class CpuCycleModel final : public ICpuCycleModel {
public:
  CpuCycleModel(Clock clock, std::uint32_t entry, Trace &trace);
  void step(Tick now, Epoch epoch, CpuPorts &ports) override;
  void reset(std::uint32_t entry) override;
  [[nodiscard]] const std::array<std::uint32_t, 32> &registers() const override {
    return registers_;
  }
  [[nodiscard]] std::uint32_t pc() const override { return pc_; }
  [[nodiscard]] bool halted() const override { return halted_; }

private:
  struct Fetch {
    Id id;
    std::uint32_t pc;
    Id generation;
  };
  struct Frame {
    Id id = 0;
    std::uint32_t pc = 0, word = 0, lhs = 0, rhs = 0, predicate = 0;
    std::optional<ErrorCode> fault;
    std::optional<rv32::Decoded> decoded;
    std::optional<rv32::Effect> effect;
    Id memory_request = 0;
  };
  void retire(const Frame &frame, std::uint32_t value, std::uint32_t next_pc, Tick now,
              Epoch epoch);
  Clock clock_;
  Trace &trace_;
  std::array<std::uint32_t, 32> registers_{};
  std::uint32_t pc_ = 0, fetch_pc_ = 0;
  std::optional<Fetch> fetch_request_;
  std::optional<Frame> fetched_, decode_, execute_;
  Id next_fetch_ = 1, next_data_ = 1, generation_ = 0;
  bool halted_ = false;
};
} // namespace qsbit
