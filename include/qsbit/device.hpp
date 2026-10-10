#pragma once

#include "qsbit/backend.hpp"
#include "qsbit/device/gates.hpp"
#include "qsbit/device/readouts.hpp"
#include "qsbit/device/resources.hpp"
#include "qsbit/trace.hpp"
#include <functional>
#include <map>
#include <optional>

namespace qsbit {
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
  struct PreparedEvents {
    std::vector<ScheduledEvent> actions;
    std::vector<qsbit::Readout> readouts;
  };
  [[nodiscard]] PreparedEvents prepare(const TriggeredEvents &batch) const;
  [[nodiscard]] std::vector<ScheduledEvent> resolve(const TriggeredEvents &batch) const;
  const DeviceProfile profile_;
  BackendExecution backend_;
  Trace &trace_;
  ResourceReservations reservations_;
  std::map<Tick, Boundary> boundaries_;
  std::map<Id, ScheduledEvent> active_;
  Readouts readouts_;
  Tick last_tick_ = 0;
  std::optional<Tick> processed_tick_;
};
} // namespace qsbit
