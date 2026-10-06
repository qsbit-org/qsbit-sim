#include "qsbit/defaults.hpp"
#include "qsbit/simulator.hpp"
#include "test.hpp"

using namespace qsbit;
class InitializationProbe final : public IQuantumBackend {
public:
  explicit InitializationProbe(bool &initialized) : initialized_(initialized) {}
  void validate(const EventSpec &) const override {}
  void reset(std::uint32_t, std::uint32_t) override { initialized_ = true; }
  std::vector<bool> execute(Epoch, std::span<const BackendOperation>) override { return {}; }

private:
  bool &initialized_;
};
// CPU mock for callback and reset tests.
class ExternalCpu final : public ICpuCycleModel {
public:
  explicit ExternalCpu(std::uint32_t entry) { reset(entry); }
  void reset(std::uint32_t entry) override {
    registers_.fill(0);
    pc_ = entry;
    index_ = 0;
  }
  void step(Tick, Epoch, CpuPorts &ports) override {
    if (halted())
      return;
    const std::array<ControlOperation, 3> operations{
        {{1, ControlKind::Wait, 8}, {2, ControlKind::Codeword, 0, 1}, {3, ControlKind::Halt}}};
    if (ports.control(operations[index_])) {
      ++index_;
      pc_ += 4;
      registers_[1] = index_;
    }
  }
  const std::array<std::uint32_t, 32> &registers() const override { return registers_; }
  std::uint32_t pc() const override { return pc_; }
  bool halted() const override { return index_ == 3; }

private:
  std::array<std::uint32_t, 32> registers_{};
  std::uint32_t pc_ = 0, index_ = 0;
};
int sc_main(int argc, char **argv) {
  try {
    const std::string scenario = argc > 1 ? argv[1] : "normal";
    const std::array<std::uint8_t, 4> bytes{0x13, 0, 0, 0};
    if (scenario == "fine_resolution" || scenario == "coarse_resolution") {
      if (scenario == "fine_resolution")
        sc_core::sc_set_time_resolution(1, sc_core::SC_PS);
      else
        sc_core::sc_set_time_resolution(10, sc_core::SC_NS);
      bool initialized = false;
      faults(ErrorCode::InvalidProfile, [&] {
        Simulator sim("sim", default_profile(), ProgramImage::raw(bytes, 0, 0, 4096),
                      std::make_unique<InitializationProbe>(initialized));
      });
      CHECK(!initialized);
      return 0;
    }
    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    const bool reset = scenario == "reset";
    Simulator sim(
        "sim", default_profile(), ProgramImage::raw(bytes, 0, 0, 4096),
        std::make_unique<MockBackend>(), reset ? std::vector<Tick>{100} : std::vector<Tick>{}, true,
        [](Clock, std::uint32_t entry, Trace &) { return std::make_unique<ExternalCpu>(entry); });
    sc_core::sc_start(5000, sc_core::SC_NS);
    CHECK(sim.success() && sim.cpu().pc() == 12 && sim.cpu().registers()[1] == 3);
    unsigned count = 0;
    for (const auto &event : sim.trace().events())
      if (event.kind == "OperationStart") {
        ++count;
        CHECK(event.tick == (reset ? 1260U : 1160U));
      }
    CHECK(count == 1);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
