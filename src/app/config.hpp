#pragma once
#include "../config_json.hpp"
#include "qsbit/backend.hpp"
#include "qsbit/core.hpp"
#include "qsbit/defaults.hpp"
#include "qsbit/sync.hpp"
#include <filesystem>
#include <span>
#include <string_view>

namespace qsbit::app {
struct RunConfig {
  Profile profile = default_profile();
  std::string program, backend_name = "mock", trace_path = "trace.jsonl",
                       summary_path = "summary.json";
  std::string module_directory, memory_dump, backend_command;
  Json backend_options = Json::object(), decoder_options = Json::object();
  BackendExecutionConfig backend_execution;
  std::string cpu_model = "rv32";
  Json core_settings = Json::array(), connection_settings = Json::array();
  std::filesystem::path config_base, simulation_config;
  bool check_config = false, trace_stalls = true;
  std::uint32_t memory_base = 0, memory_size = 65536, raw_base = 0;
  bool raw = false, reverse = false, help = false;
  std::string dump_profile;
  std::vector<Tick> resets;
  std::map<Id, bool> outcomes;
  std::vector<std::uint32_t> inspect;
};
[[nodiscard]] std::string_view usage();
void apply_run_config(RunConfig &run, const Json &config, const std::filesystem::path &base);
[[nodiscard]] RunConfig parse_run_config(std::span<const std::string> arguments,
                                         std::string module_directory = {});
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
