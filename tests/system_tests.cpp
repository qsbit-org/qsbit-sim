#include "qsbit/defaults.hpp"
#include "qsbit/simulator.hpp"
#include "test.hpp"
#include <fstream>
#include <iterator>

using namespace qsbit;
int sc_main(int argc, char **argv) {
  try {
    CHECK(argc >= 3);
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    const std::string scenario = argv[1];
    std::ifstream stream(argv[2], std::ios::binary);
    CHECK(bool(stream));
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
    auto image = ProgramImage::elf(bytes, 0, 65536);
    auto profile = default_profile();
    profile.start = 200;
    const bool outcome = scenario != "feedback_zero";
    auto backend =
        std::make_unique<ScriptedBackend>(std::map<Id, bool>{{1, outcome}, {2, outcome}});
    const bool reverse = argc > 3 && std::string(argv[3]) == "reverse";
    Simulator sim("sim", profile, std::move(image), std::move(backend), {}, reverse);
    sc_core::sc_start(sc_core::sc_time::from_value(profile.watchdog + 20));
    if (sim.fault())
      std::cerr << name(*sim.fault()) << ": " << sim.fault_message() << '\n';
    CHECK(sim.success());
    CHECK(sim.cpu().registers()[0] == 0);
    CHECK(sim.memory().read(0x1000, 4) == static_cast<std::uint32_t>(outcome));
    std::vector<TraceEvent> operations;
    for (const auto &e : sim.trace().events())
      if (e.kind == "OperationStart")
        operations.push_back(e);
    if (scenario == "bell") {
      CHECK(operations.size() == 4);
      CHECK(operations[0].operation == "h" && operations[0].tick == 360);
      CHECK(operations[1].operation == "cx" && operations[1].tick == 400);
      CHECK(operations[2].tick == 440 && operations[3].tick == 440);
      CHECK(sim.memory().read(0x1004, 4) == static_cast<std::uint32_t>(outcome));
    } else if (scenario == "feedback_one" || scenario == "feedback_zero") {
      CHECK(operations.size() == 3);
      CHECK(operations[0].tick == 360 && operations[1].tick == 440);
      CHECK(operations[2].tick == 720 && operations[2].operation == (outcome ? "x" : "z"));
    } else if (scenario == "pulse") {
      CHECK(operations.size() == 2 && operations[0].operation == "drive_x" &&
            operations[0].tick == 360);
    } else
      throw std::runtime_error("unknown system test");
    CHECK(sim.trace().events().back().kind == "SimulationCompleted");
    if (argc > 4) {
      std::ofstream out(argv[4]);
      sim.trace().write_jsonl(out);
    }
    std::cout << "PASS " << scenario << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
