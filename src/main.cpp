#include "app/config.hpp"
#include "app/output.hpp"
#include "qsbit/defaults.hpp"
#include "qsbit/simulator.hpp"
#ifdef QSBIT_HAS_PYTHON
#include "qsbit/python_backend.hpp"
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

using namespace qsbit;
using namespace qsbit::app;
int sc_main(int argc, char **argv) {
  try {
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    auto profile = default_profile();
    std::string program, backend_name = "mock", trace_path = "trace.jsonl",
                         summary_path = "summary.json";
    std::string module_directory = QSBIT_PYTHON_MODULE_DIRECTORY;
    std::string memory_dump;
    std::string backend_command;
    Json backend_options = Json::object();
    Json decoder_options = Json::object();
    BackendExecutionConfig backend_execution;
    std::string cpu_model = "rv32";
    Json core_settings = Json::array(), connection_settings = Json::array();
    std::filesystem::path config_base;
    bool check_config = false;
    bool trace_stalls = true;
    std::uint32_t memory_base = 0, memory_size = 65536, raw_base = 0;
    bool raw = false, reverse = false;
    std::vector<Tick> resets;
    std::map<Id, bool> outcomes;
    std::vector<std::uint32_t> inspect;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      const auto value = [&]() {
        require(i + 1 < argc, ErrorCode::InvalidOperand, "missing value for " + argument);
        return std::string(argv[++i]);
      };
      if (argument == "--help") {
        std::cout
            << "qsbit-sim --config FILE | --program FILE [options]\n"
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
        return 0;
      }
      if (argument == "--program")
        program = value();
      else if (argument == "--config") {
        const std::filesystem::path path = value();
        const Json config = read_json(path);
        require(config.is_object(), ErrorCode::InvalidProfile, "run config must be an object");
        if (config.contains("simulation")) {
          const bool check_only =
              argc == 4 && (check_config || std::string(argv[3]) == "--check-config");
          require(argc == 3 || check_only, ErrorCode::InvalidOperand,
                  "simulation strategies accept --config FILE and optional --check-config");
#ifdef QSBIT_HAS_PYTHON
          PythonSession session(module_directory);
          PythonBackend::run_simulation(std::filesystem::absolute(path).string(), argv[0],
                                        check_only);
          return 0;
#else
          throw Fault(ErrorCode::UnsupportedCapability,
                      "simulation strategies require Python backends");
#endif
        }
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
        const auto base = std::filesystem::absolute(path).parent_path();
        config_base = base;
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
          program = relative_to(base, config["program"], "program");
        if (config.contains("backend"))
          backend_name = json_string(config["backend"], "backend");
        if (config.contains("backend_options"))
          backend_options = config["backend_options"];
        if (config.contains("decoding")) {
          require(config["decoding"].is_object() && !config["decoding"].empty(),
                  ErrorCode::InvalidProfile, "decoding must be a nonempty object");
          decoder_options = config["decoding"];
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
            backend_execution.max_batch_operations = setting.get<std::uint32_t>();
          }
        }
        if (config.contains("cpu_model"))
          cpu_model = json_string(config["cpu_model"], "cpu_model");
        if (config.contains("trace_stalls")) {
          require(config["trace_stalls"].is_boolean(), ErrorCode::InvalidProfile,
                  "trace_stalls must be a boolean");
          trace_stalls = config["trace_stalls"].get<bool>();
        }
        if (config.contains("profile_file"))
          apply_profile(profile,
                        read_json(relative_to(base, config["profile_file"], "profile_file")));
        if (config.contains("profile"))
          apply_profile(profile, config["profile"]);
        if (config.contains("trace"))
          trace_path = relative_to(base, config["trace"], "trace");
        if (config.contains("summary"))
          summary_path = relative_to(base, config["summary"], "summary");
        if (config.contains("memory_dump"))
          memory_dump = relative_to(base, config["memory_dump"], "memory_dump");
        if (config.contains("python_path"))
          module_directory = relative_to(base, config["python_path"], "python_path");
        if (config.contains("memory_base"))
          memory_base = json_word(config["memory_base"], "memory_base");
        if (config.contains("memory_size"))
          memory_size = json_word(config["memory_size"], "memory_size");
        if (config.contains("raw_base")) {
          raw = true;
          raw_base = json_word(config["raw_base"], "raw_base");
        }
        if (config.contains("resets")) {
          require(config["resets"].is_array(), ErrorCode::InvalidProfile,
                  "resets must be an array");
          resets.clear();
          for (const auto &tick : config["resets"])
            resets.push_back(json_number(tick, "reset tick"));
        }
        if (config.contains("inspect")) {
          require(config["inspect"].is_array(), ErrorCode::InvalidProfile,
                  "inspect must be an array");
          inspect.clear();
          for (const auto &address : config["inspect"])
            inspect.push_back(json_word(address, "inspect address"));
        }
        if (config.contains("outcomes")) {
          require(config["outcomes"].is_array(), ErrorCode::InvalidProfile,
                  "outcomes must be an array");
          outcomes.clear();
          Id id = 1;
          for (const auto &outcome : config["outcomes"]) {
            require(outcome.is_boolean(), ErrorCode::InvalidProfile,
                    "outcomes must contain booleans");
            outcomes[id++] = outcome.get<bool>();
          }
        }
        if (config.contains("reverse_registration")) {
          require(config["reverse_registration"].is_boolean(), ErrorCode::InvalidProfile,
                  "reverse_registration must be a boolean");
          reverse = config["reverse_registration"].get<bool>();
        }
      } else if (argument == "--list-backends")
        backend_command = "list";
      else if (argument == "--help-backend")
        backend_command = "help";
      else if (argument == "--backend-schema")
        backend_command = "schema";
      else if (argument == "--generate-config")
        backend_command = "generate";
      else if (argument == "--check-config")
        check_config = true;
      else if (argument == "--backend")
        backend_name = value();
      else if (argument == "--cpu-model")
        cpu_model = value();
      else if (argument == "--trace")
        trace_path = value();
      else if (argument == "--summary")
        summary_path = value();
      else if (argument == "--memory-dump")
        memory_dump = value();
      else if (argument == "--python-path")
        module_directory = value();
      else if (argument == "--memory-base")
        memory_base = word(value());
      else if (argument == "--memory-size")
        memory_size = word(value());
      else if (argument == "--raw-base") {
        raw = true;
        raw_base = word(value());
      } else if (argument == "--seed")
        profile.seed = word(value());
      else if (argument == "--start")
        profile.start = number(value());
      else if (argument == "--reset")
        resets.push_back(number(value()));
      else if (argument == "--inspect")
        inspect.push_back(word(value()));
      else if (argument == "--reverse-registration")
        reverse = true;
      else if (argument == "--outcomes") {
        std::istringstream input(value());
        std::string bit;
        Id id = 1;
        while (std::getline(input, bit, ',')) {
          require(bit == "0" || bit == "1", ErrorCode::InvalidOperand,
                  "outcomes must be zero or one");
          outcomes[id++] = bit == "1";
        }
      } else if (argument == "--profile") {
        const auto path = value();
        std::ifstream input(path);
        require(bool(input), ErrorCode::InvalidProfile, "cannot read " + path);
        Json config;
        input >> config;
        apply_profile(profile, config);
      } else if (argument == "--dump-default-profile") {
        write_json(value(), profile_json(profile));
        return 0;
      } else
        throw Fault(ErrorCode::InvalidOperand, "unknown argument: " + argument);
    }
    if (!backend_command.empty()) {
#ifdef QSBIT_HAS_PYTHON
      PythonSession python(module_directory);
      std::cout << PythonBackend::inspect(backend_name, backend_command) << '\n';
      return 0;
#else
      if (backend_command == "list") {
        std::cout << "[{\"name\":\"mock\",\"missing_dependencies\":[]}]\n";
        return 0;
      }
      throw Fault(ErrorCode::UnsupportedCapability,
                  "backend configuration discovery requires QSBIT_PYTHON_BACKENDS=ON");
#endif
    }
    require(!program.empty() || !core_settings.empty(), ErrorCode::InvalidOperand,
            "a program or cores configuration is required");
    require(core_settings.empty() ||
                (program.empty() && !raw && inspect.empty() && memory_dump.empty()),
            ErrorCode::InvalidProfile,
            "cores cannot be combined with a top-level program, raw_base, inspect or memory_dump");
    require(backend_options.is_object(), ErrorCode::InvalidProfile,
            "backend_options must be an object");
    profile.validate();
    auto [cores, models] =
        configure_cores(profile, program, core_settings, cpu_model, config_base,
                        {memory_base, memory_size, raw ? std::optional{raw_base} : std::nullopt});
    const auto connections = configure_connections(connection_settings);
#ifdef QSBIT_HAS_PYTHON
    std::unique_ptr<PythonSession> python;
#endif
    std::unique_ptr<IQuantumBackend> backend;
    if (backend_name == "mock") {
      require(backend_options.empty(), ErrorCode::InvalidProfile,
              "mock backend_options must be empty; configure outcomes in the run file");
      backend = std::make_unique<MockBackend>(outcomes);
    } else {
#ifdef QSBIT_HAS_PYTHON
      python = std::make_unique<PythonSession>(module_directory);
      auto adapter = std::make_unique<PythonBackend>(
          PythonBackendConfig{backend_name, backend_options.dump()});
      backend_options = Json::parse(adapter->options());
      backend = std::move(adapter);
#else
      throw Fault(ErrorCode::UnsupportedCapability, "this build has no Python backends");
#endif
    }
    DecoderSystemConfig decoding;
    if (!decoder_options.empty()) {
#ifdef QSBIT_HAS_PYTHON
      if (!python)
        python = std::make_unique<PythonSession>(module_directory);
      decoding = python_decoders(decoder_options.dump());
#else
      throw Fault(ErrorCode::UnsupportedCapability, "decoder adapters require Python support");
#endif
    }
    Simulator sim("simulator", std::move(cores), connections, std::move(backend), resets, reverse,
                  backend_execution, std::move(decoding));
    sim.include_stalls(trace_stalls);
    if (check_config) {
      std::cout << "Configuration valid\n";
      return 0;
    }
    sc_core::sc_start(sc_core::sc_time::from_value(
        checked_add(sim.profile().watchdog, sim.profile().tcu.period)));
    {
      ensure_parent(trace_path);
      std::ofstream trace(trace_path);
      require(bool(trace), ErrorCode::InvalidOperand, "cannot open trace output");
      sim.trace().write_jsonl(trace);
    }
    auto result = simulation_summary(sim, backend_name, backend_options, backend_execution, models,
                                     connection_settings, inspect, !core_settings.empty());
    if (!decoder_options.empty())
      result["decoding"] = decoder_options;
    write_json(summary_path, result);
    if (!memory_dump.empty()) {
      ensure_parent(memory_dump);
      std::ofstream dump(memory_dump, std::ios::binary);
      const auto data = sim.memory().bytes();
      dump.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
      require(bool(dump), ErrorCode::Protocol, "memory dump failed");
    }
    std::cout << (sim.success() ? "Completed" : "Failed") << " at tick "
              << sc_core::sc_time_stamp().value() << "; trace: " << trace_path << '\n';
    return sim.success() ? 0 : 1;
  } catch (const Fault &e) {
    std::cerr << qsbit::name(e.code()) << ": " << e.what() << '\n';
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "Configuration or backend error: " << e.what() << '\n';
    return 2;
  }
}
