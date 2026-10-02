#include "qsbit/backend.hpp"

namespace qsbit {
void MockBackend::validate(const EventSpec &action) const {
  require(!action.targets.empty(), ErrorCode::UnsupportedCapability, "empty target list");
}
void MockBackend::reset(std::uint32_t qubits, std::uint32_t) {
  qubits_ = qubits;
  calls_ = 0;
}
void MockBackend::evolve(Tick from, Tick to, std::span<const EventSpec>) {
  require(to >= from, ErrorCode::BackendFailure, "backend time decreased");
  if (to > from)
    ++calls_;
}
void MockBackend::apply(std::span<const EventSpec> gates) {
  for (const auto &gate : gates)
    for (auto target : gate.targets)
      require(target < qubits_, ErrorCode::BackendFailure, "invalid backend target");
  if (!gates.empty())
    ++calls_;
}
std::vector<bool> MockBackend::measure(std::span<const MeasurementReference> references) {
  std::vector<bool> values;
  for (const auto &reference : references) {
    require(reference.target < qubits_, ErrorCode::BackendFailure, "invalid measurement target");
    const auto it = outcomes_.find(reference.measurement);
    values.push_back(it != outcomes_.end() && it->second);
  }
  if (!references.empty())
    ++calls_;
  return values;
}
} // namespace qsbit
