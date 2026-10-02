#pragma once
#include "qsbit/control.hpp"
#include "qsbit/cpu/eqasm.hpp"
#include <json.hpp>

namespace qsbit {
using Json = nlohmann::json;
void apply_profile(Profile &profile, const Json &input);
Json profile_json(const Profile &profile);
EqasmConfiguration eqasm_configuration(const Json &input);
} // namespace qsbit
