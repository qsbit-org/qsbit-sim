#include "qsbit/core.hpp"
#include "qsbit/control_command.hpp"
#include "qsbit/control_protocol.hpp"
#include "qsbit/cpu.hpp"
#include "qsbit/cpu/rv32.hpp"
#include "qsbit/error.hpp"
#include "qsbit/image.hpp"
#include "qsbit/measurement.hpp"
#include "qsbit/profile.hpp"
#include "qsbit/sync.hpp"
#include "qsbit/tcu.hpp"
#include "qsbit/time.hpp"
#include "qsbit/timing_config.hpp"
#include "qsbit/timing_control.hpp"
#include "qsbit/trace.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace qsbit {
Core::Core(std::uint32_t id, Profile profile, ProgramImage image, Trace &trace,
           SyncNetwork &network, TimingControl::ValidateAction validate, CpuFactory cpu_factory,
           std::size_t sync_capacity, bool identify)
    : id_(id), profile_(std::move(profile)),
      trace_(trace, identify ? std::optional{id} : std::nullopt), sync_(id, network, trace_),
      registers_({profile_.qubits, profile_.result_capacity, profile_.fast_feedback}),
      timing_control_(timing_config(profile_), registers_, trace_, std::move(validate),
                      [this](std::uint32_t target) { sync_.validate(target); }),
      links_(transport_config(profile_)), fetch_port_(profile_.cpu), data_port_(profile_.cpu),
      memory_(std::move(image), profile_.cpu, profile_.memory_latency),
      cpu_(cpu_factory
               ? cpu_factory(profile_.cpu, memory_.image().entry(), trace_)
               : std::make_unique<CpuCycleModel>(profile_.cpu, memory_.image().entry(), trace_)),
      tcu_(tcu_config(profile_), trace_, sync_capacity) {
  profile_.validate();
  require(bool(cpu_), ErrorCode::InvalidProfile, "CPU factory returned no model");
}
void Core::cpu_edge(Tick now, Epoch epoch) {
  timing_control_.receive(now, epoch, links_);
  CpuPorts ports{fetch_port_, data_port_, [&](const ControlOperation &operation) {
                   return timing_control_.execute(operation, now, epoch, links_);
                 }};
  cpu_->step(now, epoch, ports);
}
void Core::memory_edge(Tick now, Epoch epoch) { memory_.step(now, epoch, fetch_port_, data_port_); }
TcuOutput Core::tcu_edge(Tick now, Epoch epoch, const TcuCycleModel::Preflight &preflight) {
  const bool paused = sync_.step(now, epoch);
  const auto *request = links_.timing_events.peek(now);
  std::vector<Completion> results;
  while (auto result = links_.fast_results.take(now)) {
    if (result->epoch == epoch)
      results.push_back(result->value);
    else
      trace_.emit({now, epoch, "StaleCompletionDiscarded"});
  }
  auto output =
      tcu_.step(now, epoch, request ? &request->value : nullptr, results, preflight, paused,
                [&](std::span<const std::uint32_t> targets) { sync_.preflight(targets, now); });
  if (output.admitted) {
    const auto accepted = links_.timing_events.take(now);
    links_.replies.publish(now, epoch, EnqueueReply{accepted->value.point.label});
  }
  sync_.book(output.synchronizations, now, epoch);
  for (const auto &reference : output.fast_delivered)
    links_.fast_credits.publish(now, epoch, reference);
  if (auto end = links_.closure.take(now)) {
    require(end->epoch == epoch, ErrorCode::Protocol, "stale stream closure");
    tcu_.close(end->value);
    trace_.emit({now, epoch, "EndOfStreamVisible", 0, end->value.last_label});
  }
  return output;
}
void Core::deliver(Tick now, Epoch epoch, const Completion &result) {
  links_.cpu_results.publish(now, epoch, result);
  if (profile_.fast_feedback)
    links_.fast_results.publish(now, epoch, result);
}
bool Core::drained() const {
  return cpu_->halted() && timing_control_.closed() && !timing_control_.pending() &&
         tcu_.drained() && sync_.drained() && !registers_.deliveries_pending() && memory_.idle() &&
         links_.timing_events.empty() && links_.replies.empty() && links_.closure.empty() &&
         links_.cpu_results.empty() && links_.fast_results.empty() && links_.fast_credits.empty() &&
         fetch_port_.requests.empty() && fetch_port_.responses.empty() &&
         data_port_.requests.empty() && data_port_.responses.empty();
}
void Core::reset(Tick now) {
  links_.reset();
  fetch_port_.reset();
  data_port_.reset();
  memory_.reset();
  cpu_->reset(memory_.image().entry());
  timing_control_.reset();
  registers_.reset();
  tcu_.reset(now);
  sync_.reset();
}
} // namespace qsbit
