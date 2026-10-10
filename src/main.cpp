#include "app/config.hpp"
#include "app/output.hpp"
#include "qsbit/simulator.hpp"
#ifdef QSBIT_HAS_PYTHON
#include "qsbit/python_backend.hpp"
#endif
#include <fstream>
#include <iostream>

using namespace qsbit;
using namespace qsbit::app;
int sc_main(int argc, char **argv) {
  try {
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    const std::vector<std::string> arguments(argv + 1, argv + argc);
    const auto run = parse_run_config(arguments);
    if (run.help) {
      std::cout << usage();
      return 0;
    }
    if (!run.dump_profile.empty()) {
      write_json(run.dump_profile, profile_json(run.profile));
      return 0;
    }
    if (!run.simulation_config.empty()) {
#ifdef QSBIT_HAS_PYTHON
      PythonSession session(run.module_directory);
      PythonBackend::run_simulation(run.simulation_config.string(), argv[0], run.check_config);
      return 0;
#else
      throw Fault(ErrorCode::UnsupportedCapability,
                  "simulation strategies require Python backends");
#endif
    }
    if (!run.backend_command.empty()) {
#ifdef QSBIT_HAS_PYTHON
      PythonSession python(run.module_directory);
      std::cout << PythonBackend::inspect(run.backend_name, run.backend_command) << '\n';
      return 0;
#else
      if (run.backend_command == "list") {
        std::cout << "[{\"name\":\"mock\",\"missing_dependencies\":[]}]\n";
        return 0;
      }
      throw Fault(ErrorCode::UnsupportedCapability,
                  "backend configuration discovery requires QSBIT_PYTHON_BACKENDS=ON");
#endif
    }
    auto [cores, models] =
        configure_cores(run.cores, {run.memory_base, run.memory_size,
                                    run.raw ? std::optional{run.raw_base} : std::nullopt});
    const auto &connections = run.connections;
#ifdef QSBIT_HAS_PYTHON
    std::unique_ptr<PythonSession> python;
#endif
    std::unique_ptr<IQuantumBackend> backend;
    auto backend_options = run.backend_options;
    if (run.backend_name == "mock") {
      require(run.backend_options.empty(), ErrorCode::InvalidProfile,
              "mock backend_options must be empty; configure outcomes in the run file");
      backend = std::make_unique<MockBackend>(run.outcomes);
    } else {
#ifdef QSBIT_HAS_PYTHON
      python = std::make_unique<PythonSession>(run.module_directory);
      auto adapter = std::make_unique<PythonBackend>(
          PythonBackendConfig{run.backend_name, run.backend_options.dump()});
      backend_options = Json::parse(adapter->options());
      backend = std::move(adapter);
#else
      throw Fault(ErrorCode::UnsupportedCapability, "this build has no Python backends");
#endif
    }
    DecoderSystemConfig decoding;
    if (!run.decoder_options.empty()) {
#ifdef QSBIT_HAS_PYTHON
      if (!python)
        python = std::make_unique<PythonSession>(run.module_directory);
      decoding = python_decoders(run.decoder_options.dump());
#else
      throw Fault(ErrorCode::UnsupportedCapability, "decoder adapters require Python support");
#endif
    }
    Simulator sim("simulator", std::move(cores), connections, std::move(backend), run.resets,
                  run.reverse, run.backend_execution, std::move(decoding));
    sim.include_stalls(run.trace_stalls);
    if (run.check_config) {
      std::cout << "Configuration valid\n";
      return 0;
    }
    sc_core::sc_start(sc_core::sc_time::from_value(
        checked_add(sim.profile().watchdog, sim.profile().tcu.period)));
    {
      ensure_parent(run.trace_path);
      std::ofstream trace(run.trace_path);
      require(bool(trace), ErrorCode::InvalidOperand, "cannot open trace output");
      sim.trace().write_jsonl(trace);
    }
    auto result = simulation_summary(sim, run.backend_name, backend_options, run.backend_execution,
                                     models, run.connections, run.inspect, run.multicore);
    if (!run.decoder_options.empty())
      result["decoding"] = run.decoder_options;
    write_json(run.summary_path, result);
    if (!run.memory_dump.empty()) {
      ensure_parent(run.memory_dump);
      std::ofstream dump(run.memory_dump, std::ios::binary);
      const auto data = sim.memory().bytes();
      dump.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
      require(bool(dump), ErrorCode::Protocol, "memory dump failed");
    }
    std::cout << (sim.success() ? "Completed" : "Failed") << " at tick "
              << sc_core::sc_time_stamp().value() << "; trace: " << run.trace_path << '\n';
    return sim.success() ? 0 : 1;
  } catch (const Fault &e) {
    std::cerr << qsbit::name(e.code()) << ": " << e.what() << '\n';
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "Configuration or backend error: " << e.what() << '\n';
    return 2;
  }
}
