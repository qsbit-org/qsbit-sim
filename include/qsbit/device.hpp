#pragma once

#include "qsbit/backend.hpp"
#include "qsbit/producer.hpp"
#include "qsbit/trace.hpp"
#include <map>
#include <optional>

namespace qsbit {
class ResourceCalendar {
public:
  void check(std::span<const PhysicalAction> actions) const;
  void reserve(std::span<const PhysicalAction> actions);
  void discard_before(Tick now);
  void reset() { reservations_.clear(); }
  [[nodiscard]] const std::vector<PhysicalAction> &reservations() const { return reservations_; }

private:
  static void pair(const PhysicalAction &a, const PhysicalAction &b);
  std::vector<PhysicalAction> reservations_;
};
class DeviceRuntime {
public:
  DeviceRuntime(const Profile &profile, IQuantumBackend &backend, Trace &trace);
  void preflight(const LaunchBatch &batch) const;
  void accept(const LaunchBatch &batch);
  void process(Tick now, Epoch epoch, ControlLinks &links);
  void reset(Tick now, Epoch epoch);
  [[nodiscard]] std::optional<Tick> next_boundary() const;
  [[nodiscard]] bool drained() const {
    return boundaries_.empty() && active_.empty() && readouts_.empty();
  }
  [[nodiscard]] const ResourceCalendar &calendar() const { return calendar_; }

private:
  struct Boundary {
    std::vector<PhysicalAction> starts;
    std::vector<Id> ends, ready;
  };
  struct Readout {
    Token token;
    Tick end = 0, arm = 0, ready = 0;
    std::optional<bool> sample;
  };
  [[nodiscard]] std::vector<PhysicalAction> resolve(const LaunchBatch &batch) const;
  const Profile &profile_;
  IQuantumBackend &backend_;
  Trace &trace_;
  ResourceCalendar calendar_;
  std::map<Tick, Boundary> boundaries_;
  std::map<Id, PhysicalAction> active_;
  std::map<Id, Readout> readouts_;
  Tick last_tick_ = 0;
  std::optional<Tick> processed_tick_;
};
} // namespace qsbit
