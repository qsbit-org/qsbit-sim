#pragma once

#include "qsbit/backend.hpp"
#include "qsbit/timing_control.hpp"
#include "qsbit/trace.hpp"
#include <map>
#include <optional>

namespace qsbit {
// Simulator conflict checks for configured output intervals.
class ResourceReservations {
public:
  void check(std::span<const ScheduledEvent> actions) const;
  void reserve(std::span<const ScheduledEvent> actions);
  void discard_before(Tick now);
  void reset() { reservations_.clear(); }
  [[nodiscard]] const std::vector<ScheduledEvent> &reservations() const { return reservations_; }

private:
  static void pair(const ScheduledEvent &a, const ScheduledEvent &b);
  std::vector<ScheduledEvent> reservations_;
};
// Schedules pulse output, acquisition and discrimination against one shared state.
class ControlElectronics {
public:
  ControlElectronics(const Profile &profile, IQuantumBackend &backend, Trace &trace);
  void preflight(const TriggeredEvents &batch) const;
  void accept(const TriggeredEvents &batch);
  void process(Tick now, Epoch epoch, ControlLinks &links);
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
  };
  [[nodiscard]] std::vector<ScheduledEvent> resolve(const TriggeredEvents &batch) const;
  const Profile &profile_;
  IQuantumBackend &backend_;
  Trace &trace_;
  ResourceReservations reservations_;
  std::map<Tick, Boundary> boundaries_;
  std::map<Id, ScheduledEvent> active_;
  std::map<Id, Readout> readouts_;
  Tick last_tick_ = 0;
  std::optional<Tick> processed_tick_;
};
} // namespace qsbit
