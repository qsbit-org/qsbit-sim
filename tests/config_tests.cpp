#include "config_json.hpp"
#include "qsbit/defaults.hpp"
#include "test.hpp"
#include <algorithm>

using namespace qsbit;

void configuration() {
  const auto p = default_profile();
  Profile restored;
  apply_profile(restored, profile_json(p));
  CHECK(restored.fingerprint() == p.fingerprint());
  for (unsigned mutation = 0; mutation < 8; ++mutation) {
    auto bad = p;
    auto &gate = bad.two_qubit_gates.front();
    if (mutation == 0)
      gate.inputs.pop_back();
    if (mutation == 1)
      gate.inputs[1] = gate.inputs[0];
    if (mutation == 2)
      gate.targets[1] = gate.targets[0];
    if (mutation == 3)
      gate.targets[1] = bad.qubits;
    if (mutation == 4)
      gate.duration = 0;
    if (mutation == 5)
      bad.two_qubit_gates.push_back(gate);
    for (auto &mapping : bad.mappings)
      if (mapping.port == 0 && mapping.codeword == 6) {
        if (mutation == 6)
          mapping.actions.front().get<GateOutputSpec>().gate = "unknown";
        if (mutation == 7)
          mapping.actions.front().port = p.ports;
      }
    faults(ErrorCode::InvalidProfile, [&] { bad.validate(); });
  }
  auto changed = p;
  changed.two_qubit_gates.front().inputs[1].core = 2;
  CHECK(changed.fingerprint() != p.fingerprint());
}

void event_fields() {
  auto profile = default_profile();
  auto &mapping =
      *std::find_if(profile.mappings.begin(), profile.mappings.end(),
                    [](const auto &entry) { return entry.port == 0 && entry.codeword == 4; });
  mapping.actions.front().get<AcquireSpec>().separate_arm = true;
  EventSpec arm;
  arm.port = 1;
  arm.delay = 7;
  arm.duration = 9;
  ArmSpec spec;
  spec.operation = "arm";
  spec.targets = {0};
  arm.spec = spec;
  mapping.actions.push_back(arm);
  profile.validate();
  const auto serialized = profile_json(profile);
  Profile restored;
  apply_profile(restored, serialized);
  CHECK(profile_json(restored) == serialized);
  for (const auto &[kind, field] : {std::pair{"gate", "discriminator_delay"},
                                    {"pulse", "separate_arm"},
                                    {"acquire", "execution_flag"},
                                    {"arm", "amplitude"},
                                    {"gate_output", "targets"}}) {
    auto invalid = serialized;
    bool changed = false;
    for (auto &entry : invalid["mappings"])
      for (auto &action : entry["actions"])
        if (action["kind"] == kind) {
          action[field] = nullptr;
          changed = true;
        }
    CHECK(changed);
    faults(ErrorCode::InvalidProfile, [&] { apply_profile(restored, invalid); });
  }
  for (const auto &action : {Json(42), Json{{"kind", "gate_output"}}}) {
    auto invalid = serialized;
    invalid["mappings"][0]["actions"][0] = action;
    faults(ErrorCode::InvalidProfile, [&] { apply_profile(restored, invalid); });
  }
}

int main() {
  try {
    configuration();
    event_fields();
    std::cout << "PASS profile configuration\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
