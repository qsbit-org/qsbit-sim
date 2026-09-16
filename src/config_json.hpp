#pragma once
#include "qsbit/control.hpp"
#include <json.hpp>

namespace qsbit {
using Json = nlohmann::json;
void apply_profile(Profile &profile, const Json &input);
Json profile_json(const Profile &profile);
} // namespace qsbit
