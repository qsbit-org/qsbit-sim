#include "../config_json.hpp"
#include "config.hpp"
#include "qsbit/error.hpp"
#include "qsbit/time.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace qsbit::app {
std::string_view usage() {
  return "qsbit-sim --config FILE | --program FILE [options]\n"
         "  --config FILE (JSON run configuration; paths relative to config file)\n"
         "  --raw-base ADDRESS --backend NAME|MODULE:CLASS\n"
         "  --list-backends --help-backend --backend-schema --generate-config\n"
         "  --check-config\n"
         "  --cpu-model rv32|vliw\n"
         "  Distributed run files specify cores and sync_connections.\n"
         "    vliw: RV32I, qsbit control instructions, and 32-bit dual-codeword bundles\n"
         "  --profile FILE --trace FILE --summary FILE --seed INTEGER --start TICK\n"
         "  --memory-base ADDRESS --memory-size BYTES --outcomes 0,1,...\n"
         "  --reset TICK --inspect ADDRESS --reverse-registration --python-path DIRECTORY\n"
         "  --memory-dump FILE --dump-default-profile FILE\n";
}
namespace {
void apply_run_config(RunConfig &run, const Json &config, const std::filesystem::path &base,
                      Json &core_settings, Json &connection_settings) {
  require(config.is_object(), ErrorCode::InvalidProfile, "run config must be an object");
  const std::set<std::string> allowed{"schema",
                                      "program",
                                      "backend",
                                      "profile",
                                      "profile_file",
                                      "trace",
                                      "summary",
                                      "memory_dump",
                                      "python_path",
                                      "memory_base",
                                      "memory_size",
                                      "raw_base",
                                      "resets",
                                      "inspect",
                                      "outcomes",
                                      "reverse_registration",
                                      "backend_options",
                                      "$schema",
                                      "trace_stalls",
                                      "cpu_model",
                                      "cores",
                                      "sync_connections",
                                      "backend_execution",
                                      "decoding"};
  for (const auto &[key, ignored] : config.items()) {
    (void)ignored;
    require(allowed.contains(key), ErrorCode::InvalidProfile, "unknown run key: " + key);
  }
  require(config.contains("schema") && config["schema"] == 1, ErrorCode::InvalidProfile,
          "unsupported run schema");
  run.config_base = base;
  if (config.contains("cores")) {
    core_settings = config["cores"];
    require(core_settings.is_array() && !core_settings.empty(), ErrorCode::InvalidProfile,
            "cores must be a nonempty array");
  }
  if (config.contains("sync_connections")) {
    connection_settings = config["sync_connections"];
    require(connection_settings.is_array(), ErrorCode::InvalidProfile,
            "sync_connections must be an array");
  }
  if (config.contains("program"))
    run.program = relative_to(base, config["program"], "program");
  if (config.contains("backend"))
    run.backend_name = json_string(config["backend"], "backend");
  if (config.contains("backend_options"))
    run.backend_options = config["backend_options"];
  if (config.contains("decoding")) {
    require(config["decoding"].is_object() && !config["decoding"].empty(),
            ErrorCode::InvalidProfile, "decoding must be a nonempty object");
    run.decoder_options = config["decoding"];
  }
  if (config.contains("backend_execution")) {
    const auto &execution = config["backend_execution"];
    require(execution.is_object(), ErrorCode::InvalidProfile,
            "backend_execution must be an object");
    for (const auto &[key, setting] : execution.items()) {
      require(key == "max_batch_operations", ErrorCode::InvalidProfile,
              "unknown backend_execution key: " + key);
      require(setting.is_number_unsigned() && setting.get<std::uint64_t>() > 0 &&
                  setting.get<std::uint64_t>() <= std::numeric_limits<std::uint32_t>::max(),
              ErrorCode::InvalidProfile, "max_batch_operations must be a positive uint32");
      run.backend_execution.max_batch_operations = setting.get<std::uint32_t>();
    }
  }
  if (config.contains("cpu_model"))
    run.cpu_model = json_string(config["cpu_model"], "cpu_model");
  if (config.contains("trace_stalls")) {
    require(config["trace_stalls"].is_boolean(), ErrorCode::InvalidProfile,
            "trace_stalls must be a boolean");
    run.trace_stalls = config["trace_stalls"].get<bool>();
  }
  if (config.contains("profile_file"))
    apply_profile(run.profile,
                  read_json(relative_to(base, config["profile_file"], "profile_file")));
  if (config.contains("profile"))
    apply_profile(run.profile, config["profile"]);
  if (config.contains("trace"))
    run.trace_path = relative_to(base, config["trace"], "trace");
  if (config.contains("summary"))
    run.summary_path = relative_to(base, config["summary"], "summary");
  if (config.contains("memory_dump"))
    run.memory_dump = relative_to(base, config["memory_dump"], "memory_dump");
  if (config.contains("python_path"))
    run.module_directory = relative_to(base, config["python_path"], "python_path");
  if (config.contains("memory_base"))
    run.memory_base = json_word(config["memory_base"], "memory_base");
  if (config.contains("memory_size"))
    run.memory_size = json_word(config["memory_size"], "memory_size");
  if (config.contains("raw_base")) {
    run.raw = true;
    run.raw_base = json_word(config["raw_base"], "raw_base");
  }
  if (config.contains("resets")) {
    require(config["resets"].is_array(), ErrorCode::InvalidProfile, "resets must be an array");
    run.resets.clear();
    for (const auto &tick : config["resets"])
      run.resets.push_back(json_number(tick, "reset tick"));
  }
  if (config.contains("inspect")) {
    require(config["inspect"].is_array(), ErrorCode::InvalidProfile, "inspect must be an array");
    run.inspect.clear();
    for (const auto &address : config["inspect"])
      run.inspect.push_back(json_word(address, "inspect address"));
  }
  if (config.contains("outcomes")) {
    require(config["outcomes"].is_array(), ErrorCode::InvalidProfile, "outcomes must be an array");
    run.outcomes.clear();
    Id id = 1;
    for (const auto &outcome : config["outcomes"]) {
      require(outcome.is_boolean(), ErrorCode::InvalidProfile, "outcomes must contain booleans");
      run.outcomes[id++] = outcome.get<bool>();
    }
  }
  if (config.contains("reverse_registration")) {
    require(config["reverse_registration"].is_boolean(), ErrorCode::InvalidProfile,
            "reverse_registration must be a boolean");
    run.reverse = config["reverse_registration"].get<bool>();
  }
}
} // namespace
RunConfig parse_run_config(std::span<const std::string> arguments, std::string module_directory) {
  RunConfig run;
  Json core_settings = Json::array(), connection_settings = Json::array();
  run.module_directory = std::move(module_directory);
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const auto &argument = arguments[i];
    const auto value = [&]() {
      require(i + 1 < arguments.size(), ErrorCode::InvalidOperand, "missing value for " + argument);
      return arguments[++i];
    };
    if (argument == "--help") {
      run.help = true;
      return run;
    }
    if (argument == "--program")
      run.program = value();
    else if (argument == "--config") {
      const auto path = std::filesystem::absolute(value());
      const auto config = read_json(path);
      require(config.is_object(), ErrorCode::InvalidProfile, "run config must be an object");
      if (config.contains("simulation")) {
        const bool check_only =
            arguments.size() == 3 && (run.check_config || arguments.back() == "--check-config");
        require(arguments.size() == 2 || check_only, ErrorCode::InvalidOperand,
                "simulation strategies accept --config FILE and optional --check-config");
        run.simulation_config = path;
        run.check_config = check_only;
        return run;
      }
      apply_run_config(run, config, path.parent_path(), core_settings, connection_settings);
    } else if (argument == "--list-backends")
      run.backend_command = "list";
    else if (argument == "--help-backend")
      run.backend_command = "help";
    else if (argument == "--backend-schema")
      run.backend_command = "schema";
    else if (argument == "--generate-config")
      run.backend_command = "generate";
    else if (argument == "--check-config")
      run.check_config = true;
    else if (argument == "--backend")
      run.backend_name = value();
    else if (argument == "--cpu-model")
      run.cpu_model = value();
    else if (argument == "--trace")
      run.trace_path = value();
    else if (argument == "--summary")
      run.summary_path = value();
    else if (argument == "--memory-dump")
      run.memory_dump = value();
    else if (argument == "--python-path")
      run.module_directory = value();
    else if (argument == "--memory-base")
      run.memory_base = word(value());
    else if (argument == "--memory-size")
      run.memory_size = word(value());
    else if (argument == "--raw-base") {
      run.raw = true;
      run.raw_base = word(value());
    } else if (argument == "--seed")
      run.profile.seed = word(value());
    else if (argument == "--start")
      run.profile.start = number(value());
    else if (argument == "--reset")
      run.resets.push_back(number(value()));
    else if (argument == "--inspect")
      run.inspect.push_back(word(value()));
    else if (argument == "--reverse-registration")
      run.reverse = true;
    else if (argument == "--outcomes") {
      std::istringstream input(value());
      std::string bit;
      Id id = 1;
      while (std::getline(input, bit, ',')) {
        require(bit == "0" || bit == "1", ErrorCode::InvalidOperand,
                "outcomes must be zero or one");
        run.outcomes[id++] = bit == "1";
      }
    } else if (argument == "--profile") {
      const auto path = value();
      std::ifstream input(path);
      require(bool(input), ErrorCode::InvalidProfile, "cannot read " + path);
      Json config;
      input >> config;
      apply_profile(run.profile, config);
    } else if (argument == "--dump-default-profile") {
      run.dump_profile = value();
      return run;
    } else
      throw Fault(ErrorCode::InvalidOperand, "unknown argument: " + argument);
  }

  if (run.backend_command.empty()) {
    require(!run.program.empty() || !core_settings.empty(), ErrorCode::InvalidOperand,
            "a program or cores configuration is required");
    require(core_settings.empty() ||
                (run.program.empty() && !run.raw && run.inspect.empty() && run.memory_dump.empty()),
            ErrorCode::InvalidProfile,
            "cores cannot be combined with a top-level program, raw_base, inspect or memory_dump");
    require(run.backend_options.is_object(), ErrorCode::InvalidProfile,
            "backend_options must be an object");
    run.profile.validate();
    run.multicore = !core_settings.empty();
    run.cores =
        resolve_cores(run.profile, run.program, core_settings, run.cpu_model, run.config_base);
    run.connections = configure_connections(connection_settings);
  }
  return run;
}
} // namespace qsbit::app
