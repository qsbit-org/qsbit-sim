#include "qsbit/backend.hpp"
#include "test.hpp"
#include <array>

using namespace qsbit;

class Backend final : public IQuantumBackend {
public:
  void validate(const EventSpec &action) const override {
    require(action.operation() == "x", ErrorCode::UnsupportedCapability, "unsupported gate");
  }
  void reset(std::uint32_t, std::uint32_t) override { batches.clear(); }
  std::vector<bool> execute(Epoch epoch, std::span<const BackendOperation> operations) override {
    epochs.push_back(epoch);
    batches.emplace_back(operations.begin(), operations.end());
    if (fail)
      throw Fault(ErrorCode::BackendFailure, "numerical failure");
    std::vector<bool> results;
    for (const auto &operation : operations)
      if (const auto *measurement = std::get_if<BackendMeasurement>(&operation))
        for (const auto &reference : measurement->references)
          results.push_back(reference.target == 1);
    if (wrong_count)
      results.push_back(false);
    return results;
  }
  std::vector<std::complex<double>> state() const override {
    return {static_cast<double>(batches.size())};
  }
  std::vector<std::vector<std::complex<double>>> density_matrix() const override {
    return {state()};
  }
  std::vector<std::vector<BackendOperation>> batches;
  std::vector<Epoch> epochs;
  bool fail = false, wrong_count = false;
};

int main() {
  try {
    Backend backend;
    BackendExecution execution(backend, {3});
    execution.reset(2, 1, 7);
    EventSpec x;
    x.get<GateSpec>().targets = {0};
    std::array gates{x};
    execution.evolve(0, 10, {});
    execution.apply(10, gates);
    gates[0].get<GateSpec>().targets = {1};
    CHECK(backend.batches.empty());
    const std::array references{MeasurementReference{7, 1, 1}, MeasurementReference{7, 2, 0}};
    CHECK((execution.measure(10, references) == std::vector<bool>{true, false}));
    CHECK(backend.batches.size() == 1 && backend.batches[0].size() == 3);
    CHECK(std::get<BackendEvolution>(backend.batches[0][0]).start == 0 &&
          std::get<BackendEvolution>(backend.batches[0][0]).tick == 10);
    CHECK(std::get<BackendGates>(backend.batches[0][1]).gates[0].targets()[0] == 0);
    CHECK(std::get<BackendMeasurement>(backend.batches[0][2]).references ==
          std::vector(references.begin(), references.end()));
    execution.evolve(10, 20, {});
    execution.apply(20, std::array{x, x, x, x});
    CHECK(backend.batches.size() == 1);
    CHECK(execution.state()[0].real() == 2);
    CHECK(backend.batches[1].size() == 2 &&
          std::get<BackendGates>(backend.batches[1][1]).gates.size() == 4);
    CHECK(execution.density_matrix()[0][0].real() == 2);
    execution.evolve(20, 30, {});
    execution.apply(30, std::array{x});
    execution.evolve(30, 40, {});
    CHECK(backend.batches.size() == 3 && backend.batches[2].size() == 3);
    execution.evolve(40, 50, {});
    execution.reset(2, 1, 8, 100);
    CHECK(execution.state()[0].real() == 0);
    execution.evolve(100, 110, {});
    const std::array stale{MeasurementReference{7, 3, 0}};
    faults(ErrorCode::Protocol, [&] { (void)execution.measure(110, stale); });
    CHECK(backend.batches.empty());
    execution.flush();
    CHECK(backend.epochs.back() == 8 &&
          std::get<BackendEvolution>(backend.batches[0][0]).start == 100);
    faults(ErrorCode::Protocol, [&] { execution.evolve(100, 120, {}); });
    faults(ErrorCode::InvalidProfile, [&] { BackendExecution invalid(backend, {0}); });
    auto bad = x;
    bad.get<GateSpec>().operation = "unknown";
    faults(ErrorCode::UnsupportedCapability, [&] { execution.apply(110, std::array{x, bad}); });
    execution.flush();
    CHECK(backend.batches.size() == 1);
    for (bool fail : {false, true}) {
      backend.fail = fail;
      backend.wrong_count = !fail;
      execution.evolve(fail ? 120 : 110, fail ? 130 : 120, {});
      try {
        execution.flush();
        CHECK(false);
      } catch (const Fault &fault) {
        CHECK(fault.code() == ErrorCode::BackendFailure);
        CHECK(std::string(fault.what()).find(fail ? "ticks 120..130" : "ticks 110..120") !=
              std::string::npos);
        CHECK(std::string(fault.what()).find("epoch 8") != std::string::npos);
      }
    }
    std::cout << "PASS backend execution\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
