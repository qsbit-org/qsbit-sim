#include "qsbit/defaults.hpp"
#include "qsbit/python_backend.hpp"
#include "qsbit/simulator.hpp"
#include "test.hpp"
#include <cmath>
#include <fstream>
#include <iterator>
#include <numbers>

using namespace qsbit;
int sc_main(int argc, char **argv) {
  try {
    CHECK(argc >= 2);
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    PythonSession session(QSBIT_PYTHON_MODULE_DIRECTORY);
    const std::string scenario = argv[1];
    if (scenario == "joint") {
      PythonBackend backend("qsbit_backend", "PulseBackend");
      backend.reset(2, 1);
      ActionSpec x;
      x.kind = ActionKind::Pulse;
      x.port = 0;
      x.targets = {0};
      x.axis = "x";
      x.amplitude = std::numbers::pi / (10 * std::sqrt(2.0));
      auto z = x;
      z.axis = "z";
      z.port = 1;
      std::vector<ActionSpec> drives{x, z};
      backend.evolve(0, 10, drives);
      const auto state = backend.state();
      const auto expected = std::complex<double>{0, -1 / std::sqrt(2.0)};
      CHECK(std::abs(state[0] - expected) < 1e-12 && std::abs(state[1] - expected) < 1e-12);
      CHECK(std::abs(state[2]) < 1e-12 && std::abs(state[3]) < 1e-12);
      backend.reset(2, 1);
      std::reverse(drives.begin(), drives.end());
      backend.evolve(0, 10, drives);
      const auto reversed = backend.state();
      for (std::size_t i = 0; i < state.size(); ++i)
        CHECK(std::abs(state[i] - reversed[i]) < 1e-12);
      CHECK(sc_core::sc_time_stamp() == sc_core::SC_ZERO_TIME);
    } else if (scenario == "capability") {
      PythonBackend backend("qsbit_backend", "AerBackend");
      backend.reset(2, 1);
      ActionSpec pulse;
      pulse.kind = ActionKind::Pulse;
      pulse.targets = {0};
      faults(ErrorCode::UnsupportedCapability, [&] { backend.validate(pulse); });
      CHECK(std::norm(backend.state()[0]) == 1.0);
    } else {
      CHECK(argc == 3);
      std::ifstream stream(argv[2], std::ios::binary);
      CHECK(bool(stream));
      const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
      auto p = default_profile();
      if (scenario == "overlap") {
        p.mappings.clear();
        for (std::uint32_t port : {0U, 1U}) {
          ActionSpec drive;
          drive.kind = ActionKind::Pulse;
          drive.port = port;
          drive.targets = {0};
          drive.resources = {{0, false}};
          drive.axis = port == 0 ? "x" : "z";
          drive.operation = "drive_" + drive.axis;
          drive.amplitude = std::numbers::pi / (20 * std::sqrt(2.0));
          p.mappings.push_back({port, 5, {drive}});
        }
      }
      auto image = ProgramImage::elf(bytes, 0, 65536);
      auto backend = std::make_unique<PythonBackend>(
          "qsbit_backend",
          (scenario == "pulse" || scenario == "overlap") ? "PulseBackend" : "AerBackend");
      Simulator sim("sim", p, std::move(image), std::move(backend));
      sc_core::sc_start(sc_core::sc_time::from_value(p.watchdog + 20));
      if (sim.fault())
        std::cerr << name(*sim.fault()) << ": " << sim.fault_message() << '\n';
      CHECK(sim.success());
      const auto state = sim.backend().state();
      double norm = 0;
      for (const auto value : state)
        norm += std::norm(value);
      CHECK(std::abs(norm - 1) < 1e-12);
      std::vector<Tick> starts;
      for (const auto &event : sim.trace().events())
        if (event.kind == "OperationStart")
          starts.push_back(event.tick);
      CHECK(starts.front() == 1160);
      if (scenario == "bell") {
        const auto first = sim.memory().read(0x1000, 4), second = sim.memory().read(0x1004, 4);
        CHECK(first <= 1 && first == second);
        CHECK(std::norm(state[first ? 3 : 0]) > 1 - 1e-12);
        CHECK((starts == std::vector<Tick>{1160, 1200, 1240, 1240}));
      } else if (scenario == "feedback") {
        CHECK(sim.memory().read(0x1000, 4) == 1 && std::norm(state[3]) > 1 - 1e-12);
        CHECK((starts == std::vector<Tick>{1160, 1240, 3560}));
      } else if (scenario == "pulse") {
        CHECK(sim.memory().read(0x1000, 4) == 1 && std::norm(state[1]) > 1 - 1e-12);
        CHECK((starts == std::vector<Tick>{1160, 1240}));
      } else if (scenario == "overlap") {
        const auto expected = std::complex<double>{0, -1 / std::sqrt(2.0)};
        CHECK(std::abs(state[0] - expected) < 1e-12 && std::abs(state[1] - expected) < 1e-12);
        CHECK(std::abs(state[2]) < 1e-12 && std::abs(state[3]) < 1e-12);
        CHECK((starts == std::vector<Tick>{1160, 1160}));
      } else
        throw std::runtime_error("unknown numerical scenario");
    }
    std::cout << "PASS numerical " << scenario << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
