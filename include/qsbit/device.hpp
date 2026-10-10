#pragma once

#include "qsbit/backend.hpp"
#include "qsbit/device/resources.hpp"
#include "qsbit/trace.hpp"
#include <functional>
#include <map>
#include <optional>

namespace qsbit {
struct DeviceProfile {
  std::uint32_t qubits, seed;
  std::map<std::string, TwoQubitGate> gates;
  [[nodiscard]] const TwoQubitGate &gate(const std::string &name) const;
};
[[nodiscard]] DeviceProfile device_profile(const Profile &profile);
// Schedules gate and pulse output, acquisition, and result delivery on the shared backend state.
class ControlElectronics {
public:
  ControlElectronics(DeviceProfile profile, IQuantumBackend &backend, Trace &trace,
                     BackendExecutionConfig execution = {});
  [[nodiscard]] BackendExecution &backend() { return backend_; }
  void validate(const EventSpec &action) const;
  void preflight(const TriggeredEvents &batch) const;
  void accept(const TriggeredEvents &batch);
  using Deliver = std::function<void(const Completion &)>;
  void process(Tick now, Epoch epoch, const Deliver &deliver);
  void finalize(Tick now);
  void reset(Tick now, Epoch epoch);
  [[nodiscard]] std::optional<Tick> next_boundary() const;
  [[nodiscard]] bool drained() const {
    return boundaries_.empty() && active_.empty() && readouts_.empty();
  }
  [[nodiscard]] const ResourceReservations &resources() const { return reservations_; }

private:
  struct Boundary {
    std::vector<ScheduledEvent> starts;
    std::vector<Id> ends, ready;
  };
  struct Readout {
    MeasurementReference reference;
    Tick end = 0, arm = 0, ready = 0;
    std::optional<bool> sample;
    std::optional<std::uint32_t> core = {};
  };
  [[nodiscard]] std::vector<ScheduledEvent> resolve(const TriggeredEvents &batch) const;
  [[nodiscard]] EventSpec gate_action(const std::string &name) const;
  const DeviceProfile profile_;
  BackendExecution backend_;
  Trace &trace_;
  ResourceReservations reservations_;
  std::map<Tick, Boundary> boundaries_;
  std::map<Id, ScheduledEvent> active_;
  std::map<Id, Readout> readouts_;
  Tick last_tick_ = 0;
  std::optional<Tick> processed_tick_;
};
} // namespace qsbit
