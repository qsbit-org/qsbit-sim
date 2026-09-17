#include "config_json.hpp"
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
} // namespace
int sc_main(int argc, char **argv) {
  try {
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    auto profile = default_profile();
    std::string program, backend_name = "scripted", trace_path = "trace.jsonl",
                         summary_path = "summary.json";
    std::string module_directory = QSBIT_PYTHON_MODULE_DIRECTORY;
    std::string memory_dump;
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
               "  --raw-base ADDRESS --backend scripted|aer|pulse\n"
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
        const std::set<std::string> allowed{
            "schema",       "program",     "backend",     "profile",
            "profile_file", "trace",       "summary",     "memory_dump",
            "python_path",  "memory_base", "memory_size", "raw_base",
            "resets",       "inspect",     "outcomes",    "reverse_registration"};
        for (const auto &[key, ignored] : config.items()) {
          (void)ignored;
          require(allowed.contains(key), ErrorCode::InvalidProfile, "unknown run key: " + key);
        }
        require(config.contains("schema") && config["schema"] == 1, ErrorCode::InvalidProfile,
                "unsupported run schema");
        const auto base = std::filesystem::absolute(path).parent_path();
        if (config.contains("program"))
          program = relative_to(base, config["program"], "program");
        if (config.contains("backend"))
          backend_name = json_string(config["backend"], "backend");
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
      } else if (argument == "--backend")
        backend_name = value();
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
    require(!program.empty(), ErrorCode::InvalidOperand, "--program is required");
    profile.validate();
    std::ifstream input(program, std::ios::binary);
    require(bool(input), ErrorCode::InvalidImage, "cannot read program");
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    auto image = raw ? ProgramImage::raw(bytes, raw_base, memory_base, memory_size)
                     : ProgramImage::elf(bytes, memory_base, memory_size);
    std::unique_ptr<IQuantumBackend> backend;
#ifdef QSBIT_HAS_PYTHON
    std::unique_ptr<PythonSession> python;
#endif
    if (backend_name == "scripted")
      backend = std::make_unique<ScriptedBackend>(outcomes);
    else {
      require(backend_name == "aer" || backend_name == "pulse", ErrorCode::InvalidOperand,
              "unknown backend");
#ifdef QSBIT_HAS_PYTHON
      python = std::make_unique<PythonSession>(module_directory);
      backend = std::make_unique<PythonBackend>(
          "qsbit_backend", backend_name == "aer" ? "AerBackend" : "PulseBackend");
#else
      throw Fault(ErrorCode::UnsupportedCapability, "this build has no Python backends");
#endif
    }
    Simulator sim("simulator", profile, std::move(image), std::move(backend), resets, reverse);
    sc_core::sc_start(
        sc_core::sc_time::from_value(checked_add(profile.watchdog, profile.tcu.period)));
    {
      ensure_parent(trace_path);
      std::ofstream trace(trace_path);
      require(bool(trace), ErrorCode::InvalidOperand, "cannot open trace output");
      sim.trace().write_jsonl(trace);
    }
    Json result{{"schema", 1},
                {"success", sim.success()},
                {"stop_tick", sc_core::sc_time_stamp().value()},
                {"backend", backend_name},
                {"configuration", profile_json(profile)},
                {"configuration_hash", profile.fingerprint()},
                {"registers", sim.cpu().registers()},
                {"pc", sim.cpu().pc()},
                {"statevector", Json::array()},
                {"memory", Json::object()},
                {"result_slots", Json::array()}};
    if (sim.fault()) {
      result["fault"] = qsbit::name(*sim.fault());
      result["message"] = sim.fault_message();
    }
    for (const auto &amplitude : sim.backend().state())
      result["statevector"].push_back({amplitude.real(), amplitude.imag()});
    for (auto address : inspect)
      result["memory"][std::to_string(address)] = sim.memory().read(address, 4);
    for (const auto &slot : sim.scoreboard().slots())
      if (slot.state != Scoreboard::State::Free)
        result["result_slots"].push_back({{"handle", slot.token.handle},
                                          {"measurement", slot.token.measurement},
                                          {"visible", slot.state == Scoreboard::State::Visible},
                                          {"value", slot.value}});
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
