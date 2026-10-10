#pragma once

#include "qsbit/control_command.hpp"
#include "qsbit/isa.hpp"
#include "qsbit/memory.hpp"
#include "qsbit/trace.hpp"
#include <array>
#include <functional>

namespace qsbit {
struct CpuPorts {
  MemoryPort &fetch;
  MemoryPort &data;
  std::function<std::optional<std::uint32_t>(const ControlOperation &)> control;
};
class ICpuCycleModel {
public:
  virtual ~ICpuCycleModel() = default;
  virtual void step(Tick now, Epoch epoch, CpuPorts &ports) = 0;
  virtual void reset(std::uint32_t entry) = 0;
  [[nodiscard]] virtual const std::array<std::uint32_t, 32> &registers() const = 0;
  [[nodiscard]] virtual std::uint32_t pc() const = 0;
  [[nodiscard]] virtual bool halted() const = 0;
};
} // namespace qsbit
