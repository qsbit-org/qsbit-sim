#pragma once
#include "../config_json.hpp"
#include "qsbit/core.hpp"
#include "qsbit/sync.hpp"
#include <filesystem>

namespace qsbit::app {
std::uint64_t number(const std::string &text);
std::uint32_t word(const std::string &text);
std::uint64_t json_number(const Json &value, const std::string &key);
std::uint32_t json_word(const Json &value, const std::string &key);
std::string json_string(const Json &value, const std::string &key);
Json read_json(const std::filesystem::path &path);
std::string relative_to(const std::filesystem::path &base, const Json &value,
                        const std::string &key);
void ensure_parent(const std::string &path);
void write_json(const std::string &path, const Json &value);
struct MemoryConfig {
  std::uint32_t base, size;
  std::optional<std::uint32_t> raw_base;
};
struct CoreSetup {
  std::vector<CoreConfig> cores;
  std::vector<std::string> models;
};
CoreSetup configure_cores(const Profile &profile, const std::string &program, const Json &settings,
                          const std::string &cpu_model, const std::filesystem::path &base,
                          MemoryConfig memory);
std::vector<SyncConnection> configure_connections(const Json &settings);
} // namespace qsbit::app
