#include "qsbit/simulator.hpp"
#include <algorithm>

namespace qsbit {
namespace {
Profile validated(Profile p) {
  p.validate();
  return p;
}
sc_core::sc_time time_at(Tick tick) { return sc_core::sc_time::from_value(tick); }
std::unique_ptr<IQuantumBackend> validated(std::unique_ptr<IQuantumBackend> backend) {
  require(bool(backend), ErrorCode::InvalidProfile, "a backend is required");
  return backend;
}
} // namespace
Simulator::Simulator(sc_core::sc_module_name name, Profile profile, ProgramImage image,
                     std::unique_ptr<IQuantumBackend> backend, std::vector<Tick> resets,
                     bool reverse_registration, CpuFactory cpu_factory)
    : sc_module(name), profile_(validated(std::move(profile))),
      backend_(validated(std::move(backend))), scoreboard_(profile_),
      producer_(profile_, scoreboard_, trace_,
                [this](const ActionSpec &a) { backend_->validate(a); }),
      links_(profile_), fetch_port_(profile_.cpu), data_port_(profile_.cpu),
      memory_(std::move(image), profile_.cpu, profile_.memory_latency),
      cpu_(cpu_factory
               ? cpu_factory(profile_.cpu, memory_.image().entry(), trace_)
               : std::make_unique<CpuCycleModel>(profile_.cpu, memory_.image().entry(), trace_)),
      tcu_(profile_, trace_), device_(profile_, *backend_, trace_),
      cpu_clock_("cpu_clock", time_at(profile_.cpu.period), 0.5, time_at(profile_.cpu.phase)),
      tcu_clock_("tcu_clock", time_at(profile_.tcu.period), 0.5, time_at(profile_.tcu.phase)),
      resets_(std::move(resets)) {
  require(bool(cpu_), ErrorCode::InvalidProfile, "CPU factory returned no model");
  require(sc_core::sc_get_time_resolution() == sc_core::sc_time(1, sc_core::SC_NS),
          ErrorCode::InvalidProfile, "v1 requires a 1 ns SystemC time resolution");
  require(std::is_sorted(resets_.begin(), resets_.end()) &&
              std::adjacent_find(resets_.begin(), resets_.end()) == resets_.end(),
          ErrorCode::InvalidProfile, "reset ticks must be sorted and unique");
  for (auto tick : resets_)
    require(tick > 0 && tick < profile_.watchdog, ErrorCode::InvalidProfile,
            "reset tick outside run");
  if (reverse_registration) {
    SC_METHOD(tcu_edge);
    sensitive << tcu_clock_.posedge_event();
    dont_initialize();
    SC_METHOD(memory_edge);
    sensitive << cpu_clock_.posedge_event();
    dont_initialize();
    SC_METHOD(cpu_edge);
    sensitive << cpu_clock_.posedge_event();
    dont_initialize();
  } else {
    SC_METHOD(cpu_edge);
    sensitive << cpu_clock_.posedge_event();
    dont_initialize();
    SC_METHOD(memory_edge);
    sensitive << cpu_clock_.posedge_event();
    dont_initialize();
    SC_METHOD(tcu_edge);
    sensitive << tcu_clock_.posedge_event();
    dont_initialize();
  }
  SC_METHOD(wakeup);
  sensitive << wake_;
  dont_initialize();
  SC_METHOD(barrier);
  sensitive << barrier_;
  dont_initialize();
  TraceEvent event{0, epoch_, "SessionStarted"};
  event.detail = profile_.fingerprint();
  trace_.emit(std::move(event));
  schedule_wakeup();
}
bool Simulator::reset_at(Tick now) {
  if (!std::binary_search(resets_.begin(), resets_.end(), now))
    return false;
  if (last_reset_ != now) {
    epoch_ = checked_add(epoch_, 1);
    links_.reset();
    fetch_port_.reset();
    data_port_.reset();
    memory_.reset();
    cpu_->reset(memory_.image().entry());
    producer_.reset();
    scoreboard_.reset();
    tcu_.reset(now);
    device_.reset(now, epoch_);
    last_reset_ = now;
    trace_.emit({now, epoch_, "SessionReset"});
  }
  return true;
}
void Simulator::fail(const Fault &fault) {
  if (stopped_)
    return;
  fault_ = fault.code();
  fault_message_ = fault.what();
  stopped_ = true;
  TraceEvent event{sc_core::sc_time_stamp().value(), epoch_, "Fault"};
  event.operation = qsbit::name(fault.code());
  event.detail = fault.what();
  trace_.emit(std::move(event));
  sc_core::sc_stop();
}
void Simulator::cpu_edge() {
  if (stopped_)
    return;
  const Tick now = sc_core::sc_time_stamp().value();
  try {
    if (!reset_at(now)) {
      producer_.receive(now, epoch_, links_);
      CpuPorts ports{fetch_port_, data_port_, [&](const ProducerOperation &op) {
                       return producer_.execute(op, now, epoch_, links_);
                     }};
      cpu_->step(now, epoch_, ports);
    }
    cpu_done_ = now;
    barrier_.notify(sc_core::SC_ZERO_TIME);
  } catch (const Fault &fault) {
    fail(fault);
  } catch (const std::exception &e) {
    fail(Fault(ErrorCode::Protocol, e.what()));
  }
}
void Simulator::memory_edge() {
  if (stopped_)
    return;
  const Tick now = sc_core::sc_time_stamp().value();
  try {
    if (!reset_at(now))
      memory_.step(now, epoch_, fetch_port_, data_port_);
    memory_done_ = now;
    barrier_.notify(sc_core::SC_ZERO_TIME);
  } catch (const Fault &fault) {
    fail(fault);
  } catch (const std::exception &e) {
    fail(Fault(ErrorCode::Protocol, e.what()));
  }
}
void Simulator::tcu_edge() {
  if (stopped_)
    return;
  const Tick now = sc_core::sc_time_stamp().value();
  try {
    if (!reset_at(now)) {
      const auto *group = links_.groups.peek(now);
      std::vector<Completion> results;
      while (auto result = links_.fast_results.take(now)) {
        if (result->epoch == epoch_)
          results.push_back(result->value);
        else
          trace_.emit({now, epoch_, "StaleCompletionDiscarded"});
      }
      auto output = tcu_.step(now, epoch_, group ? &group->value : nullptr, results,
                              [&](const LaunchBatch &batch) { device_.preflight(batch); });
      if (output.admitted) {
        const auto accepted = links_.groups.take(now);
        links_.replies.publish(now, epoch_, GroupReply{accepted->value.point.label});
      }
      if (output.launch)
        device_.accept(*output.launch);
      for (const auto &token : output.fast_delivered)
        links_.fast_credits.publish(now, epoch_, token);
      if (auto end = links_.closure.take(now)) {
        require(end->epoch == epoch_, ErrorCode::Protocol, "stale stream closure");
        tcu_.close(end->value);
        trace_.emit({now, epoch_, "EndOfStreamVisible", 0, end->value.last_label});
      }
    }
    tcu_done_ = now;
    barrier_.notify(sc_core::SC_ZERO_TIME);
  } catch (const Fault &fault) {
    fail(fault);
  } catch (const std::exception &e) {
    fail(Fault(ErrorCode::Protocol, e.what()));
  }
}
void Simulator::wakeup() {
  if (stopped_)
    return;
  try {
    (void)reset_at(sc_core::sc_time_stamp().value());
    barrier_.notify(sc_core::SC_ZERO_TIME);
  } catch (const Fault &fault) {
    fail(fault);
  }
}
bool Simulator::links_empty() const {
  return links_.groups.empty() && links_.replies.empty() && links_.closure.empty() &&
         links_.cpu_results.empty() && links_.fast_results.empty() && links_.fast_credits.empty() &&
         fetch_port_.requests.empty() && fetch_port_.responses.empty() &&
         data_port_.requests.empty() && data_port_.responses.empty();
}
void Simulator::barrier() {
  if (stopped_)
    return;
  const Tick now = sc_core::sc_time_stamp().value();
  if (profile_.cpu.edge(now) && (cpu_done_ != now || memory_done_ != now))
    return;
  if (profile_.tcu.edge(now) && tcu_done_ != now)
    return;
  try {
    if (!reset_at(now) && device_.next_boundary() == now)
      device_.process(now, epoch_, links_);
    if (cpu_->halted() && producer_.closed() && !producer_.pending() && tcu_.drained() &&
        device_.drained() && !scoreboard_.deliveries_pending() && links_empty() && memory_.idle()) {
      success_ = true;
      stopped_ = true;
      trace_.emit({now, epoch_, "SimulationCompleted"});
      sc_core::sc_stop();
      return;
    }
    require(now < profile_.watchdog, ErrorCode::Watchdog,
            "simulation failed to drain before watchdog");
    schedule_wakeup();
  } catch (const Fault &fault) {
    fail(fault);
  } catch (const std::exception &e) {
    fail(Fault(ErrorCode::BackendFailure, e.what()));
  }
}
void Simulator::schedule_wakeup() {
  const Tick now = sc_core::sc_time_stamp().value();
  Tick next = profile_.watchdog;
  if (auto device = device_.next_boundary())
    next = std::min(next, *device);
  const auto reset = std::upper_bound(resets_.begin(), resets_.end(), now);
  if (reset != resets_.end())
    next = std::min(next, *reset);
  require(next > now, ErrorCode::Protocol, "unprocessed event at or before completed barrier");
  wake_.cancel();
  wake_.notify(time_at(next - now));
}
} // namespace qsbit
