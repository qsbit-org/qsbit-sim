#include "qsbit/backend.hpp"

namespace qsbit {
void ScriptedBackend::validate(const ActionSpec &action) const {
  require(!action.targets.empty(), ErrorCode::UnsupportedCapability, "empty target list");
}
void ScriptedBackend::reset(std::uint32_t qubits, std::uint32_t) {
  qubits_ = qubits;
  calls_ = 0;
}
void ScriptedBackend::evolve(Tick from, Tick to, std::span<const ActionSpec>) {
  require(to >= from, ErrorCode::BackendFailure, "backend time decreased");
  if (to > from)
    ++calls_;
}
void ScriptedBackend::apply(std::span<const ActionSpec> gates) {
  for (const auto &gate : gates)
    for (auto target : gate.targets)
      require(target < qubits_, ErrorCode::BackendFailure, "invalid backend target");
  if (!gates.empty())
    ++calls_;
}
std::vector<bool> ScriptedBackend::measure(std::span<const Token> tokens) {
  std::vector<bool> values;
  for (const auto &token : tokens) {
    require(token.target < qubits_, ErrorCode::BackendFailure, "invalid measurement target");
    const auto it = outcomes_.find(token.measurement);
    values.push_back(it != outcomes_.end() && it->second);
  }
  if (!tokens.empty())
    ++calls_;
  return values;
}
} // namespace qsbit
