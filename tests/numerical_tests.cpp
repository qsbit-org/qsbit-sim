#include "qsbit/defaults.hpp"
#include "qsbit/python_backend.hpp"
#include "qsbit/simulator.hpp"
#include "test.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>

using namespace qsbit;
int sc_main(int argc, char **argv) {
  try {
    CHECK(argc >= 2);
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    PythonSession session(QSBIT_PYTHON_MODULE_DIRECTORY);
    const std::string scenario = argv[1];
    if (scenario == "final_state") {
      // wait.i 8; cw.i.i 0, 1; wait.i 100; sim_exit
      const std::array<std::uint32_t, 6> words{
          (8U << 15) | 0x200bU, 0x0610000bU, (100U << 15) | 0x200bU,
          0x00000513U,          0x05d00893U, 0x00000073U};
      std::vector<std::uint8_t> bytes;
      for (const auto word : words)
        for (unsigned shift = 0; shift < 32; shift += 8)
          bytes.push_back(static_cast<std::uint8_t>(word >> shift));
      auto p = default_profile();
      auto backend = std::make_unique<PythonBackend>(PythonBackendConfig{
          "aer", R"({"method":"density_matrix","noise":{"model":"thermal_relaxation",
          "qubits":[{"qubit":0,"t1_ns":1000,"t2_ns":2000,"excited_state_population":0}]}})"});
      Simulator sim("sim", p, ProgramImage::raw(bytes, 0, 0, 4096), std::move(backend));
      sc_core::sc_start(sc_core::sc_time::from_value(p.watchdog + 20));
      CHECK(sim.success());
      std::vector<Tick> starts, ends;
      for (const auto &event : sim.trace().events()) {
        if (event.kind == "OperationStart")
          starts.push_back(event.tick);
        if (event.kind == "OperationEnd")
          ends.push_back(event.tick);
      }
      CHECK((starts == std::vector<Tick>{1160}));
      CHECK((ends == std::vector<Tick>{1180}));
      CHECK(sc_core::sc_time_stamp().value() == 3160);
      const double excited = std::exp(-2000.0 / 1000.0);
      const std::array<double, 4> populations{1.0 - excited, excited, 0.0, 0.0};
      const auto density = sim.backend().density_matrix();
      CHECK(density.size() == populations.size());
      for (std::size_t row = 0; row < density.size(); ++row)
        for (std::size_t column = 0; column < density[row].size(); ++column) {
          const double expected = row == column ? populations[row] : 0.0;
          CHECK(std::abs(density[row][column] - expected) < 1e-12);
        }
    } else if (scenario == "capability") {
      PythonBackend backend(PythonBackendConfig{"aer"});
      backend.reset(2, 1);
      EventSpec pulse;
      pulse.spec = PulseSpec{};
      pulse.get<PulseSpec>().operands.targets = {0};
      faults(ErrorCode::UnsupportedCapability, [&] { backend.validate(pulse); });
      CHECK(std::norm(backend.state()[0]) == 1.0);
    } else {
      CHECK(argc == 3);
      std::ifstream stream(argv[2], std::ios::binary);
      CHECK(bool(stream));
      const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
      auto p = default_profile();
      p.start = 200;
      auto image = ProgramImage::elf(bytes, 0, 65536);
      auto backend = std::make_unique<PythonBackend>(PythonBackendConfig{"aer"});
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
      if (scenario == "bell") {
        const auto first = sim.memory().read(0x1000, 4), second = sim.memory().read(0x1004, 4);
        CHECK(first <= 1 && first == second);
        CHECK(std::norm(state[first ? 3 : 0]) > 1 - 1e-12);
      } else if (scenario == "feedback") {
        CHECK(sim.memory().read(0x1000, 4) == 1 && std::norm(state[3]) > 1 - 1e-12);
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
