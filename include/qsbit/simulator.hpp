#pragma once

#include "qsbit/core.hpp"
#include "qsbit/decoder.hpp"
#include "qsbit/device.hpp"
#include <systemc>

namespace qsbit {
// The device barrier runs after every clocked process due at the current tick.
class Simulator final : public sc_core::sc_module {
public:
  using CpuFactory = Core::CpuFactory;
  using sc_core::sc_module::trace;
  Simulator(sc_core::sc_module_name name, Profile profile, ProgramImage image,
            std::unique_ptr<IQuantumBackend> backend, std::vector<Tick> resets = {},
            bool reverse_registration = false, CpuFactory cpu_factory = {},
            BackendExecutionConfig execution = {}, DecoderSystemConfig decoding = {});
  Simulator(sc_core::sc_module_name name, std::vector<CoreConfig> cores,
            std::vector<SyncConnection> connections, std::unique_ptr<IQuantumBackend> backend,
            std::vector<Tick> resets = {}, bool reverse_registration = false,
            BackendExecutionConfig execution = {}, DecoderSystemConfig decoding = {});
  [[nodiscard]] const Trace &trace() const { return trace_; }
  void include_stalls(bool enabled) { trace_.include_stalls(enabled); }
  [[nodiscard]] const Core &core(std::size_t index) const { return *cores_.at(index); }
  [[nodiscard]] std::size_t core_count() const { return cores_.size(); }
  [[nodiscard]] const ICpuCycleModel &cpu() const { return core(0).cpu(); }
  [[nodiscard]] const ProgramImage &memory() const { return core(0).memory(); }
  [[nodiscard]] const MeasurementRegisters &measurement_registers() const {
    return core(0).measurement_registers();
  }
  [[nodiscard]] BackendExecution &backend() { return device_.backend(); }
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
  TriggeredEvents device_events(const TriggeredEvents &batch, std::size_t index) const;
  const Profile profile_;
  Trace trace_;
  DecoderSystem decoders_;
  std::unique_ptr<IQuantumBackend> backend_;
  SyncNetwork network_;
  std::vector<std::unique_ptr<Core>> cores_;
  std::vector<std::uint32_t> port_offsets_;
  ControlElectronics device_;
  std::vector<std::unique_ptr<sc_core::sc_clock>> cpu_clocks_;
  sc_core::sc_clock tcu_clock_;
  sc_core::sc_event wake_, barrier_;
  std::optional<Tick> cpu_done_, memory_done_, tcu_done_, last_reset_;
  std::vector<Tick> resets_;
  Epoch epoch_ = 1;
  bool reverse_ = false, stopped_ = false, success_ = false;
  std::optional<ErrorCode> fault_;
  std::string fault_message_;
};
} // namespace qsbit
