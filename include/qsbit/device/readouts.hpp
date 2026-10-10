#pragma once

#include "qsbit/control_protocol.hpp"
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace qsbit {
struct Readout {
  MeasurementReference reference;
  Tick end = 0, arm = 0, ready = 0;
  std::optional<bool> sample;
  std::optional<std::uint32_t> core = {};
};
class Readouts {
public:
  [[nodiscard]] std::vector<Readout> prepare(std::span<const ScheduledEvent> actions,
                                             Epoch epoch) const;
  void insert(std::span<const Readout> readouts);
  void sample(Id measurement, bool value);
  [[nodiscard]] const Readout &at(Id measurement) const;
  [[nodiscard]] Completion result(Id measurement) const;
  void erase(Id measurement) { pending_.erase(measurement); }
  void reset() { pending_.clear(); }
  [[nodiscard]] bool empty() const { return pending_.empty(); }

private:
  std::map<Id, Readout> pending_;
};
} // namespace qsbit
