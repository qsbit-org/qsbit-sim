#include "qsbit/backend.hpp"
#include <set>

namespace qsbit {
BackendExecution::BackendExecution(IQuantumBackend &backend, BackendExecutionConfig config)
    : backend_(backend), config_(config) {
  require(config_.max_batch_operations > 0, ErrorCode::InvalidProfile,
          "backend_execution.max_batch_operations must be positive");
}
void BackendExecution::reset(std::uint32_t qubits, std::uint32_t seed, Epoch epoch, Tick now) {
  pending_.clear();
  epoch_ = epoch;
  committed_tick_ = now;
  backend_.reset(qubits, seed);
}
void BackendExecution::commit(BackendOperation operation) {
  require(operation.tick >= committed_tick_, ErrorCode::Protocol, "backend time decreased");
  committed_tick_ = operation.tick;
  pending_.push_back(std::move(operation));
  if (pending_.size() == config_.max_batch_operations)
    flush();
}
void BackendExecution::evolve(Tick from, Tick to, std::span<const EventSpec> drives) {
  require(from == committed_tick_ && to >= from, ErrorCode::Protocol,
          "backend evolution must cover the next time interval");
  if (to > from)
    commit({BackendOperation::Kind::Evolve, from, to, {drives.begin(), drives.end()}, {}});
}
void BackendExecution::apply(Tick now, std::span<const EventSpec> gates) {
  for (const auto &gate : gates)
    validate(gate);
  for (const auto &gate : gates)
    commit({BackendOperation::Kind::Apply, now, now, {gate}, {}});
}
std::vector<bool> BackendExecution::measure(Tick now,
                                            std::span<const MeasurementReference> references) {
  if (references.empty())
    return {};
  require(now >= committed_tick_, ErrorCode::Protocol, "backend measurement time decreased");
  std::set<std::uint32_t> targets;
  for (const auto &reference : references)
    require(reference.epoch == epoch_ && targets.insert(reference.target).second,
            ErrorCode::Protocol, "invalid measurement epoch or duplicate target");
  committed_tick_ = now;
  pending_.push_back(
      {BackendOperation::Kind::Measure, now, now, {}, {references.begin(), references.end()}});
  return execute(references.size());
}
std::vector<bool> BackendExecution::execute(std::size_t expected_results) {
  if (pending_.empty())
    return {};
  const auto first = pending_.front().from;
  const auto last = pending_.back().tick;
  try {
    auto results = backend_.execute(epoch_, pending_);
    require(results.size() == expected_results, ErrorCode::BackendFailure,
            "backend returned wrong measurement count");
    pending_.clear();
    return results;
  } catch (const std::exception &error) {
    pending_.clear();
    throw Fault(ErrorCode::BackendFailure, "backend batch epoch " + std::to_string(epoch_) +
                                               " ticks " + std::to_string(first) + ".." +
                                               std::to_string(last) + ": " + error.what());
  }
}
void BackendExecution::flush() { (void)execute(0); }
std::vector<std::complex<double>> BackendExecution::state() {
  flush();
  return backend_.state();
}
std::vector<std::vector<std::complex<double>>> BackendExecution::density_matrix() {
  flush();
  return backend_.density_matrix();
}
void MockBackend::validate(const EventSpec &action) const {
  require(!action.targets.empty(), ErrorCode::UnsupportedCapability, "empty target list");
}
void MockBackend::reset(std::uint32_t qubits, std::uint32_t) {
  qubits_ = qubits;
  calls_ = 0;
}
std::vector<bool> MockBackend::execute(Epoch epoch, std::span<const BackendOperation> operations) {
  std::vector<bool> values;
  for (const auto &operation : operations) {
    require(operation.tick >= operation.from, ErrorCode::BackendFailure, "backend time decreased");
    for (const auto &action : operation.actions)
      for (auto target : action.targets)
        require(target < qubits_, ErrorCode::BackendFailure, "invalid backend target");
    for (const auto &reference : operation.references) {
      require(reference.epoch == epoch && reference.target < qubits_, ErrorCode::BackendFailure,
              "invalid measurement reference");
      const auto it = outcomes_.find(reference.measurement);
      values.push_back(it != outcomes_.end() && it->second);
    }
  }
  ++calls_;
  return values;
}
} // namespace qsbit
