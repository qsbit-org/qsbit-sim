#include "qsbit/simulator.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace qsbit {
namespace {
Profile validated(const std::vector<CoreConfig> &cores, const std::vector<SyncConnection> &links) {
  require(!cores.empty(), ErrorCode::InvalidProfile, "at least one core is required");
  const auto &first = cores.front().profile;
  std::set<std::uint32_t> ids;
  std::uint64_t ports = 0;
  for (const auto &core : cores) {
    core.profile.validate();
    require(core.id <= 131071 && ids.insert(core.id).second, ErrorCode::InvalidProfile,
            "core addresses must be unique 17-bit integers");
    const auto &p = core.profile;
    require(p.tcu.period == first.tcu.period && p.tcu.phase == first.tcu.phase &&
                p.qubits == first.qubits && p.seed == first.seed && p.watchdog == first.watchdog,
            ErrorCode::InvalidProfile,
            "cores must share the TCU clock, qubit count, seed and watchdog");
    ports = checked_add(ports, p.ports);
  }
  require(ports <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::InvalidProfile,
          "total output port count exceeds uint32");
  for (const auto &link : links)
    require(ids.contains(link.first) && ids.contains(link.second), ErrorCode::InvalidProfile,
            "synchronization connection names an unknown core");
  return first;
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
    : Simulator(name,
                std::vector<CoreConfig>{
                    {0, std::move(profile), std::move(image), std::move(cpu_factory)}},
                {}, std::move(backend), std::move(resets), reverse_registration) {}
Simulator::Simulator(sc_core::sc_module_name name, std::vector<CoreConfig> configs,
                     std::vector<SyncConnection> connections,
                     std::unique_ptr<IQuantumBackend> backend, std::vector<Tick> resets,
                     bool reverse_registration)
    : sc_module(name), profile_(validated(configs, connections)),
      backend_(validated(std::move(backend))), network_(profile_.tcu, connections),
      device_(profile_, *backend_, trace_),
      tcu_clock_("tcu_clock", time_at(profile_.tcu.period), 0.5, time_at(profile_.tcu.phase)),
      resets_(std::move(resets)), reverse_(reverse_registration) {
  require(sc_core::sc_get_time_resolution() == sc_core::sc_time(1, sc_core::SC_NS),
          ErrorCode::InvalidProfile, "SystemC time resolution must be 1 ns");
  require(std::is_sorted(resets_.begin(), resets_.end()) &&
              std::adjacent_find(resets_.begin(), resets_.end()) == resets_.end(),
          ErrorCode::InvalidProfile, "reset ticks must be sorted and unique");
  for (auto tick : resets_)
    require(tick > 0 && tick < profile_.watchdog, ErrorCode::InvalidProfile,
            "reset tick outside run");
  std::uint32_t offset = 0;
  for (auto &config : configs) {
    port_offsets_.push_back(offset);
    const auto clock = config.profile.cpu;
    const auto index = cores_.size();
    cores_.push_back(std::make_unique<Core>(
        config.id, config.profile, std::move(config.image), trace_, network_,
        [this, index](const EventSpec &event) {
          auto mapped = event;
          mapped.port += port_offsets_.at(index);
          backend_->validate(mapped);
        },
        std::move(config.cpu_factory), config.sync_capacity, configs.size() > 1));
    offset += config.profile.ports;
    cpu_clocks_.push_back(
        std::make_unique<sc_core::sc_clock>(sc_core::sc_gen_unique_name("cpu_clock"),
                                            time_at(clock.period), 0.5, time_at(clock.phase)));
  }
  if (reverse_) {
    SC_METHOD(tcu_edge);
    sensitive << tcu_clock_.posedge_event();
    dont_initialize();
    SC_METHOD(memory_edge);
    for (const auto &clock : cpu_clocks_)
      sensitive << clock->posedge_event();
    dont_initialize();
    SC_METHOD(cpu_edge);
    for (const auto &clock : cpu_clocks_)
      sensitive << clock->posedge_event();
    dont_initialize();
  } else {
    SC_METHOD(cpu_edge);
    for (const auto &clock : cpu_clocks_)
      sensitive << clock->posedge_event();
    dont_initialize();
    SC_METHOD(memory_edge);
    for (const auto &clock : cpu_clocks_)
      sensitive << clock->posedge_event();
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
    network_.reset();
    for (auto &core : cores_)
      core->reset(now);
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
    if (!reset_at(now))
      for (auto &core : cores_)
        if (core->profile().cpu.edge(now))
          core->cpu_edge(now, epoch_);
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
      for (auto &core : cores_)
        if (core->profile().cpu.edge(now))
          core->memory_edge(now, epoch_);
    memory_done_ = now;
    barrier_.notify(sc_core::SC_ZERO_TIME);
  } catch (const Fault &fault) {
    fail(fault);
  } catch (const std::exception &e) {
    fail(Fault(ErrorCode::Protocol, e.what()));
  }
}
TriggeredEvents Simulator::device_events(const TriggeredEvents &batch, std::size_t index) const {
  auto mapped = batch;
  if (cores_.size() == 1)
    return mapped;
  const auto global_id = [&](Id id) {
    require(id > 0, ErrorCode::Protocol, "zero event or measurement identity");
    return checked_add(checked_mul(id - 1, cores_.size()), index + 1);
  };
  for (auto &event : mapped.events) {
    event.id = global_id(event.id);
    event.core = cores_.at(index)->id();
    event.action.port += port_offsets_.at(index);
    if (event.reference)
      event.reference->measurement = global_id(event.reference->measurement);
  }
  return mapped;
}
void Simulator::tcu_edge() {
  if (stopped_)
    return;
  const Tick now = sc_core::sc_time_stamp().value();
  try {
    if (!reset_at(now)) {
      for (std::size_t n = 0; n < cores_.size(); ++n) {
        const auto i = reverse_ ? cores_.size() - n - 1 : n;
        auto output = cores_[i]->tcu_edge(now, epoch_, [&](const TriggeredEvents &batch) {
          device_.preflight(device_events(batch, i));
        });
        if (output.launch)
          device_.accept(device_events(*output.launch, i));
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
void Simulator::barrier() {
  if (stopped_)
    return;
  const Tick now = sc_core::sc_time_stamp().value();
  const bool cpu_edge_now = std::any_of(
      cores_.begin(), cores_.end(), [now](const auto &c) { return c->profile().cpu.edge(now); });
  if (cpu_edge_now && (cpu_done_ != now || memory_done_ != now))
    return;
  if (profile_.tcu.edge(now) && tcu_done_ != now)
    return;
  try {
    if (!reset_at(now) && device_.next_boundary() == now)
      device_.process(now, epoch_, [&](const Completion &result) {
        auto local = result;
        require(local.reference.measurement > 0, ErrorCode::Protocol, "zero measurement identity");
        const auto index = (local.reference.measurement - 1) % cores_.size();
        local.reference.measurement = (local.reference.measurement - 1) / cores_.size() + 1;
        cores_.at(index)->deliver(now, epoch_, local);
      });
    if (device_.drained() && network_.empty() &&
        std::all_of(cores_.begin(), cores_.end(), [](const auto &c) { return c->drained(); })) {
      device_.finalize(now);
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
