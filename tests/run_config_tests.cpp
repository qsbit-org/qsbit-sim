#include "app/config.hpp"
#include "test.hpp"
#include <fstream>

using namespace qsbit;
using namespace qsbit::app;

RunConfig parse(std::initializer_list<std::string> arguments) {
  return parse_run_config(std::vector<std::string>(arguments), "python-modules");
}

int main(int argc, char **argv) {
  try {
    CHECK(argc == 2);
    const auto directory = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(directory);
    const auto path = (directory / "run.json").string();
    const Json config{{"schema", 1},
                      {"program", "program.elf"},
                      {"profile_file", "profile.json"},
                      {"profile", {{"seed", 3}}},
                      {"trace", "out/trace.jsonl"},
                      {"summary", "out/summary.json"},
                      {"memory_dump", "out/memory.bin"},
                      {"python_path", "modules"},
                      {"resets", {100, 200}},
                      {"inspect", {4096}},
                      {"outcomes", {false, true}},
                      {"backend_execution", {{"max_batch_operations", 7}}},
                      {"decoding", {{"mmio_base", 1073741824}}},
                      {"trace_stalls", false}};
    std::ofstream(directory / "profile.json") << Json{{"seed", 2}};
    std::ofstream(path) << config;
    const auto run = parse({"--seed", "1", "--config", path, "--seed", "4", "--trace", "cli.jsonl",
                            "--reset", "300", "--inspect", "4100", "--check-config"});
    CHECK(run.profile.seed == 4 && run.program == (directory / "program.elf").string());
    CHECK(run.trace_path == "cli.jsonl" &&
          run.summary_path == (directory / "out/summary.json").string());
    CHECK(run.memory_dump == (directory / "out/memory.bin").string());
    CHECK(run.module_directory == (directory / "modules").string());
    CHECK((run.resets == std::vector<Tick>{100, 200, 300}));
    CHECK((run.inspect == std::vector<std::uint32_t>{4096, 4100}));
    CHECK(!run.outcomes.at(1) && run.outcomes.at(2));
    CHECK(run.backend_execution.max_batch_operations == 7 && !run.trace_stalls && run.check_config);
    CHECK(run.decoder_options == config["decoding"]);
    CHECK(parse({"--seed", "1", "--config", path}).profile.seed == 3);
    CHECK(!std::filesystem::exists(directory / "out"));
    for (const auto &invalid :
         {Json{{"unknown", true}}, Json{{"schema", 2}}, Json{{"resets", {-1}}},
          Json{{"inspect", {4294967296ULL}}}, Json{{"outcomes", {1}}},
          Json{{"trace_stalls", "false"}}, Json{{"backend_options", nullptr}},
          Json{{"decoding", Json::object()}}, Json{{"cores", Json::array()}},
          Json{{"sync_connections", nullptr}}, Json{{"program", 1}},
          Json{{"backend_execution", {{"max_batch_operations", 0}}}},
          Json{{"backend_execution", {{"unknown", 1}}}}}) {
      auto bad = config;
      bad.update(invalid);
      std::ofstream(path) << bad;
      faults(ErrorCode::InvalidProfile, [&] { (void)parse({"--config", path}); });
    }
    const Json cores{{"schema", 1},
                     {"cores", Json::array({Json{{"id", 0}, {"program", "program.elf"}}})}};
    std::ofstream(path) << cores;
    CHECK(parse({"--config", path}).core_settings == cores["cores"]);
    for (const auto &option : {"--program", "--raw-base", "--inspect", "--memory-dump"})
      faults(ErrorCode::InvalidProfile, [&] { (void)parse({"--config", path, option, "0"}); });
    std::ofstream(path) << Json{{"simulation", Json::object()}};
    for (const auto &arguments : {std::vector<std::string>{"--config", path, "--check-config"},
                                  std::vector<std::string>{"--check-config", "--config", path}}) {
      const auto repeated = parse_run_config(arguments);
      CHECK(repeated.check_config && repeated.simulation_config == path);
    }
    faults(ErrorCode::InvalidOperand, [&] { (void)parse({"--config", path, "--seed", "1"}); });
    CHECK(parse({"--help"}).help);
    CHECK(parse({"--list-backends"}).backend_command == "list");
    const auto output = directory / "default-profile.json";
    CHECK(parse({"--dump-default-profile", output.string()}).dump_profile == output.string());
    CHECK(!std::filesystem::exists(output));
    faults(ErrorCode::InvalidOperand, [&] { (void)parse({"--program"}); });
    faults(ErrorCode::InvalidOperand, [&] { (void)parse({"--unknown"}); });
    std::cout << "PASS run configuration\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
