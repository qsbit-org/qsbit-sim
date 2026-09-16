#pragma once

#include "qsbit/cpu.hpp"
#include "qsbit/device.hpp"
#include "qsbit/tcu.hpp"
#include <memory>
#include <systemc>

namespace qsbit {
// Clocked owners use committed mailboxes. DeviceRuntime has an explicit tick barrier
// that waits for every coincident owner, independently of process registration order.
class Simulator final : public sc_core::sc_module {
public:
  using CpuFactory = std::function<std::unique_ptr<ICpuCycleModel>(Clock, std::uint32_t, Trace &)>;
  SC_HAS_PROCESS(Simulator);
  Simulator(sc_core::sc_module_name name, Profile profile, ProgramImage image,
            std::unique_ptr<IQuantumBackend> backend, std::vector<Tick> resets = {},
            bool reverse_registration = false, CpuFactory cpu_factory = {});
  [[nodiscard]] const Trace &trace() const { return trace_; }
  [[nodiscard]] const ICpuCycleModel &cpu() const { return *cpu_; }
  [[nodiscard]] const ProgramImage &memory() const { return memory_.image(); }
  [[nodiscard]] const Scoreboard &scoreboard() const { return scoreboard_; }
  [[nodiscard]] const IQuantumBackend &backend() const { return *backend_; }
  [[nodiscard]] bool success() const { return success_; }
  [[nodiscard]] const std::optional<ErrorCode> &fault() const { return fault_; }
  [[nodiscard]] const std::string &fault_message() const { return fault_message_; }
  [[nodiscard]] const Profile &profile() const { return profile_; }

private:
  void cpu_edge();
  void memory_edge();
  void tcu_edge();
  void wakeup();
  void barrier();
  bool reset_at(Tick now);
  void schedule_wakeup();
  void fail(const Fault &fault);
  [[nodiscard]] bool links_empty() const;
  const Profile profile_;
  Trace trace_;
  std::unique_ptr<IQuantumBackend> backend_;
  Scoreboard scoreboard_;
  TimelineProducer producer_;
  ControlLinks links_;
  MemoryPort fetch_port_, data_port_;
  MemoryModel memory_;
  std::unique_ptr<ICpuCycleModel> cpu_;
  TcuCycleModel tcu_;
  DeviceRuntime device_;
  sc_core::sc_clock cpu_clock_, tcu_clock_;
  sc_core::sc_event wake_, barrier_;
  std::optional<Tick> cpu_done_, memory_done_, tcu_done_, last_reset_;
  std::vector<Tick> resets_;
  Epoch epoch_ = 1;
  bool stopped_ = false, success_ = false;
  std::optional<ErrorCode> fault_;
  std::string fault_message_;
};
} // namespace qsbit
