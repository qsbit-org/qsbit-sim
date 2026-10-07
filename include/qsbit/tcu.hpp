#pragma once

#include "qsbit/control.hpp"
#include "qsbit/feedback.hpp"
#include "qsbit/trace.hpp"
#include <deque>
#include <functional>
#include <span>

namespace qsbit {
struct TcuOutput {
  bool admitted = false;
  std::optional<TriggeredEvents> launch;
  std::vector<MeasurementReference> fast_delivered;
  std::vector<std::uint32_t> synchronizations;
};
class TcuCycleModel {
public:
  using Preflight = std::function<void(const TriggeredEvents &)>;
  using SyncPreflight = std::function<void(std::span<const std::uint32_t>)>;
  TcuCycleModel(const Profile &profile, Trace &trace, std::size_t sync_capacity = 8);
  TcuOutput step(Tick now, Epoch epoch, const TimingEvents *candidate,
                 const std::vector<Completion> &results, const Preflight &preflight,
                 bool paused = false, const SyncPreflight &sync_preflight = {});
  void close(const EndOfStream &end);
  void reset(Tick epoch_origin = 0);
  [[nodiscard]] bool drained() const;
  [[nodiscard]] std::size_t timing_size() const { return timing_.size(); }
  [[nodiscard]] std::size_t port_size(std::uint32_t port) const { return events_.at(port).size(); }
  [[nodiscard]] Id last_label() const { return last_label_; }
  [[nodiscard]] Tick last_due() const { return last_due_; }
  [[nodiscard]] const ExecutionFlags &execution_flags() const { return execution_flags_; }

private:
  struct Point {
    TimingPoint point;
    Tick due;
  };
  const Profile &profile_;
  Trace &trace_;
  std::deque<Point> timing_;
  std::vector<std::deque<OperationEvent>> events_;
  ExecutionFlags execution_flags_;
  Tick last_due_ = 0;
  Tick start_ = 0;
  Tick paused_ticks_ = 0;
  std::size_t sync_capacity_ = 8, sync_size_ = 0;
  Id last_label_ = 0;
  bool closed_ = false;
  bool accept_wait_ = false, allow_underflow_ = false, paused_ = false;
};
} // namespace qsbit
