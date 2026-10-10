#pragma once
#include "qsbit/control_links.hpp"
#include "qsbit/timing_control.hpp"

#include "qsbit/cpu.hpp"
#include "qsbit/profile.hpp"
#include "qsbit/sync.hpp"
#include "qsbit/tcu.hpp"
#include <memory>

namespace qsbit {
class Core {
public:
  void attach_device(std::function<bool(std::uint32_t)> contains,
                     MemoryModel::DeviceAccess access) {
    memory_.attach_device(std::move(contains), std::move(access));
  }
  using CpuFactory = std::function<std::unique_ptr<ICpuCycleModel>(Clock, std::uint32_t, Trace &)>;
  Core(std::uint32_t id, Profile profile, ProgramImage image, Trace &trace, SyncNetwork &network,
       TimingControl::ValidateAction validate, CpuFactory cpu_factory = {},
       std::size_t sync_capacity = 8, bool identify = true);
  void cpu_edge(Tick now, Epoch epoch);
  void memory_edge(Tick now, Epoch epoch);
  TcuOutput tcu_edge(Tick now, Epoch epoch, const TcuCycleModel::Preflight &preflight);
  void deliver(Tick now, Epoch epoch, const Completion &result);
  void reset(Tick now);
  [[nodiscard]] bool drained() const;
  [[nodiscard]] const Profile &profile() const { return profile_; }
  [[nodiscard]] const ICpuCycleModel &cpu() const { return *cpu_; }
  [[nodiscard]] const ProgramImage &memory() const { return memory_.image(); }
  [[nodiscard]] const MeasurementRegisters &measurement_registers() const { return registers_; }
  [[nodiscard]] std::uint32_t id() const { return id_; }

private:
  std::uint32_t id_;
  const Profile profile_;
  Trace trace_;
  SyncUnit sync_;
  MeasurementRegisters registers_;
  TimingControl timing_control_;
  ControlLinks links_;
  MemoryPort fetch_port_, data_port_;
  MemoryModel memory_;
  std::unique_ptr<ICpuCycleModel> cpu_;
  TcuCycleModel tcu_;
};
struct CoreConfig {
  std::uint32_t id;
  Profile profile;
  ProgramImage image;
  Core::CpuFactory cpu_factory = {};
  std::size_t sync_capacity = 8;
};
} // namespace qsbit
