#include "config_json.hpp"
#include "qsbit/cpu/vliw.hpp"
#include "qsbit/defaults.hpp"
#include "qsbit/simulator.hpp"
#ifdef QSBIT_HAS_PYTHON
#include "qsbit/python_backend.hpp"
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>

using namespace qsbit;
namespace {
std::uint64_t number(const std::string &text) {
  require(!text.empty() && text.front() != '-', ErrorCode::InvalidOperand,
          "expected a nonnegative integer");
  std::size_t end = 0;
  const auto value = std::stoull(text, &end, 0);
  require(end == text.size(), ErrorCode::InvalidOperand, "invalid integer argument");
  return value;
}
std::uint32_t word(const std::string &text) {
  const auto value = number(text);
  require(value <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::InvalidOperand,
          "argument exceeds uint32");
  return static_cast<std::uint32_t>(value);
}
std::uint64_t json_number(const Json &value, const std::string &key) {
  require(value.is_number_integer() &&
              (value.is_number_unsigned() || value.get<std::int64_t>() >= 0),
          ErrorCode::InvalidProfile, key + " must be a nonnegative integer");
  return value.get<std::uint64_t>();
}
std::uint32_t json_word(const Json &value, const std::string &key) {
  const auto result = json_number(value, key);
  require(result <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::InvalidProfile,
          key + " exceeds uint32");
  return static_cast<std::uint32_t>(result);
}
std::string json_string(const Json &value, const std::string &key) {
  require(value.is_string(), ErrorCode::InvalidProfile, key + " must be a string");
  return value.get<std::string>();
}
Json read_json(const std::filesystem::path &path) {
  std::ifstream stream(path);
  require(bool(stream), ErrorCode::InvalidProfile, "cannot read " + path.string());
  Json value;
  stream >> value;
  return value;
}
std::string relative_to(const std::filesystem::path &base, const Json &value,
                        const std::string &key) {
  return (base / json_string(value, key)).lexically_normal().string();
}
void ensure_parent(const std::string &path) {
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
}
void write_json(const std::string &path, const Json &value) {
  ensure_parent(path);
  std::ofstream stream(path);
  require(bool(stream), ErrorCode::InvalidOperand, "cannot open " + path);
  stream << value.dump(2) << '\n';
  require(bool(stream), ErrorCode::Protocol, "cannot write " + path);
}
Core::CpuFactory cpu_factory(const std::string &model) {
  require(model == "rv32" || model == "vliw", ErrorCode::InvalidProfile,
          "cpu_model must be rv32 or vliw");
  if (model == "vliw")
    return [](Clock clock, std::uint32_t entry, Trace &trace) {
      return std::make_unique<VliwCpuCycleModel>(clock, entry, trace);
    };
  return {};
}
} // namespace
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
               "    vliw: RV32I, HISQ instructions and 32-bit dual-cw bundles\n"
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
                                            "backend_execution"};
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
    const auto load_image = [&](const std::string &path) {
      std::ifstream input(path, std::ios::binary);
      require(bool(input), ErrorCode::InvalidImage, "cannot read program: " + path);
      const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
      return raw ? ProgramImage::raw(bytes, raw_base, memory_base, memory_size)
                 : ProgramImage::elf(bytes, memory_base, memory_size);
    };
    std::vector<CoreConfig> cores;
    std::vector<std::string> models;
    if (core_settings.empty()) {
      cores.push_back({0, profile, load_image(program), cpu_factory(cpu_model)});
      models.push_back(cpu_model);
    } else {
      const std::set<std::string> allowed{"id",           "program",   "profile",
                                          "profile_file", "cpu_model", "sync_capacity"};
      for (const auto &settings : core_settings) {
        require(settings.is_object() && settings.contains("id") && settings.contains("program"),
                ErrorCode::InvalidProfile, "each core requires id and program");
        for (const auto &[key, ignored] : settings.items()) {
          (void)ignored;
          require(allowed.contains(key), ErrorCode::InvalidProfile, "unknown core key: " + key);
        }
        auto p = profile;
        if (settings.contains("profile_file"))
          apply_profile(
              p, read_json(relative_to(config_base, settings["profile_file"], "profile_file")));
        if (settings.contains("profile"))
          apply_profile(p, settings["profile"]);
        const auto model = settings.contains("cpu_model")
                               ? json_string(settings["cpu_model"], "cpu_model")
                               : cpu_model;
        cores.push_back({json_word(settings["id"], "core id"), p,
                         load_image(relative_to(config_base, settings["program"], "program")),
                         cpu_factory(model),
                         settings.contains("sync_capacity")
                             ? json_word(settings["sync_capacity"], "sync_capacity")
                             : 8});
        models.push_back(model);
      }
    }
    std::vector<SyncConnection> connections;
    for (const auto &settings : connection_settings) {
      const std::set<std::string> allowed{"first", "second", "first_to_second", "second_to_first",
                                          "capacity"};
      require(settings.is_object(), ErrorCode::InvalidProfile, "sync connection must be an object");
      for (const auto &[key, ignored] : settings.items()) {
        (void)ignored;
        require(allowed.contains(key), ErrorCode::InvalidProfile,
                "unknown sync connection key: " + key);
      }
      for (auto key : {"first", "second", "first_to_second", "second_to_first"})
        require(settings.contains(key), ErrorCode::InvalidProfile,
                std::string("missing sync connection key: ") + key);
      connections.push_back(
          {json_word(settings["first"], "first"), json_word(settings["second"], "second"),
           json_number(settings["first_to_second"], "first_to_second"),
           json_number(settings["second_to_first"], "second_to_first"),
           settings.contains("capacity") ? json_word(settings["capacity"], "capacity") : 8});
    }
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
    Simulator sim("simulator", std::move(cores), connections, std::move(backend), resets, reverse,
                  backend_execution);
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
    Json result{
        {"schema", 1},
        {"success", sim.success()},
        {"stop_tick", sc_core::sc_time_stamp().value()},
        {"backend", backend_name},
        {"backend_options", backend_options},
        {"backend_execution", {{"max_batch_operations", backend_execution.max_batch_operations}}},
        {"cpu_model", cpu_model},
        {"configuration", profile_json(profile)},
        {"configuration_hash", profile.fingerprint()},
        {"registers", sim.cpu().registers()},
        {"pc", sim.cpu().pc()},
        {"statevector", Json::array()},
        {"memory", Json::object()},
        {"measurement_registers", Json::array()}};
    if (sim.fault()) {
      result["fault"] = qsbit::name(*sim.fault());
      result["message"] = sim.fault_message();
    }
    if (!core_settings.empty()) {
      result["cores"] = Json::array();
      for (std::size_t i = 0; i < sim.core_count(); ++i) {
        const auto &core = sim.core(i);
        Json registers = Json::array();
        for (const auto &reg : core.measurement_registers().registers())
          registers.push_back(
              {{"pending", reg.pending}, {"valid", reg.pending == 0}, {"value", reg.value}});
        result["cores"].push_back({{"id", core.id()},
                                   {"cpu_model", models[i]},
                                   {"configuration", profile_json(core.profile())},
                                   {"registers", core.cpu().registers()},
                                   {"pc", core.cpu().pc()},
                                   {"drained", core.drained()},
                                   {"measurement_registers", registers}});
      }
      result["sync_connections"] = connection_settings;
      for (auto key : {"registers", "pc", "cpu_model", "configuration", "configuration_hash",
                       "measurement_registers"})
        result.erase(key);
    }
    for (const auto &amplitude : sim.backend().state())
      result["statevector"].push_back({amplitude.real(), amplitude.imag()});
    const auto density = sim.backend().density_matrix();
    if (!density.empty()) {
      result["density_matrix"] = Json::array();
      for (const auto &row : density) {
        Json values = Json::array();
        for (const auto &entry : row)
          values.push_back({entry.real(), entry.imag()});
        result["density_matrix"].push_back(std::move(values));
      }
    }
    for (auto address : inspect)
      result["memory"][std::to_string(address)] = sim.memory().read(address, 4);
    if (core_settings.empty())
      for (const auto &reg : sim.measurement_registers().registers())
        result["measurement_registers"].push_back(
            {{"pending", reg.pending}, {"valid", reg.pending == 0}, {"value", reg.value}});
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
