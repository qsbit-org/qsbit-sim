#pragma once

#include "qsbit/control.hpp"
#include "qsbit/feedback.hpp"
#include "qsbit/trace.hpp"
#include <deque>
#include <functional>

namespace qsbit {
struct TcuOutput {
  bool admitted = false;
  std::optional<LaunchBatch> launch;
  std::vector<Token> fast_delivered;
};
class TcuCycleModel {
public:
  using Preflight = std::function<void(const LaunchBatch &)>;
  TcuCycleModel(const Profile &profile, Trace &trace);
  TcuOutput step(Tick now, Epoch epoch, const Group *candidate,
                 const std::vector<Completion> &results, const Preflight &preflight);
  void close(const EndOfStream &end);
  void reset(Tick epoch_origin = 0);
  [[nodiscard]] bool drained() const;
  [[nodiscard]] std::size_t timing_size() const { return timing_.size(); }
  [[nodiscard]] std::size_t port_size(std::uint32_t port) const { return events_.at(port).size(); }
  [[nodiscard]] Id last_label() const { return last_label_; }
  [[nodiscard]] Tick last_due() const { return last_due_; }
  [[nodiscard]] const FastHistory &history() const { return history_; }

private:
  struct Point {
    TimingPoint point;
    Tick due;
  };
  const Profile &profile_;
  Trace &trace_;
  std::deque<Point> timing_;
  std::vector<std::deque<ReservedEvent>> events_;
  FastHistory history_;
  Tick last_due_ = 0;
  Tick start_ = 0;
  Id last_label_ = 0;
  bool closed_ = false;
};
} // namespace qsbit
