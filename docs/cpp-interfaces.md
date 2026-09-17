# C++ Interface Reference

These declarations define the CPU/TCU adapter and control records.
The [behavioral protocol](module-architecture.md#4-baseline-protocol-and-event-ordering)
defines ordering; [external interfaces](interfaces.md) define JSON and program formats.

## CPU adapter

[include/qsbit/cpu.hpp](../include/qsbit/cpu.hpp)

<!-- source: {"path": "include/qsbit/cpu.hpp", "start": "struct CpuPorts {", "end": "class CpuCycleModel final"} -->
```cpp
struct CpuPorts {
  MemoryPort &fetch;
  MemoryPort &data;
  std::function<std::optional<std::uint32_t>(const ProducerOperation &)> control;
};
// Adapter contract intentionally contains no SystemC types or concrete pipeline latches.
class ICpuCycleModel {
public:
  virtual ~ICpuCycleModel() = default;
  virtual void step(Tick now, Epoch epoch, CpuPorts &ports) = 0;
  virtual void reset(std::uint32_t entry) = 0;
  [[nodiscard]] virtual const std::array<std::uint32_t, 32> &registers() const = 0;
  [[nodiscard]] virtual std::uint32_t pc() const = 0;
  [[nodiscard]] virtual bool halted() const = 0;
};
```
<!-- /source -->

## TCU transition

[include/qsbit/tcu.hpp](../include/qsbit/tcu.hpp)

<!-- source: {"path": "include/qsbit/tcu.hpp", "start": "struct TcuOutput {", "end": "} // namespace qsbit"} -->
```cpp
struct TcuOutput {
  bool admitted = false;
  std::optional<LaunchBatch> launch;
  std::vector<Token> fast_delivered;
};
class TcuCycleModel {
public:
  using Preflight = std::function<void(const LaunchBatch &)>;
  TcuCycleModel(const Profile &profile, Trace &trace);
  TcuOutput step(Tick now, Epoch epoch, const Group *candidate,
                 const std::vector<Completion> &results, const Preflight &preflight);
  void close(const EndOfStream &end);
  void reset(Tick epoch_origin = 0);
  [[nodiscard]] bool drained() const;
  [[nodiscard]] std::size_t timing_size() const { return timing_.size(); }
  [[nodiscard]] std::size_t port_size(std::uint32_t port) const { return events_.at(port).size(); }
  [[nodiscard]] Id last_label() const { return last_label_; }
  [[nodiscard]] Tick last_due() const { return last_due_; }
  [[nodiscard]] const FastHistory &history() const { return history_; }

private:
  struct Point {
    TimingPoint point;
    Tick due;
  };
  const Profile &profile_;
  Trace &trace_;
  std::deque<Point> timing_;
  std::vector<std::deque<ReservedEvent>> events_;
  FastHistory history_;
  Tick last_due_ = 0;
  Tick start_ = 0;
  Id last_label_ = 0;
  bool closed_ = false;
};
```
<!-- /source -->

## Control records

[include/qsbit/control.hpp](../include/qsbit/control.hpp)

<!-- source: {"path": "include/qsbit/control.hpp", "start": "struct Token {", "end": "struct PhysicalAction {"} -->
```cpp
struct Token {
  Epoch epoch = 0;
  Id measurement = 0;
  std::uint32_t slot = 0, generation = 0, handle = 0, target = 0;
  bool operator==(const Token &) const = default;
};
struct Condition {
  Token token;
  bool expected = false;
};
struct ReservedEvent {
  Epoch epoch = 0;
  Id id = 0, instruction = 0, label = 0;
  std::uint32_t source_port = 0, codeword = 0;
  ActionSpec action;
  std::optional<Token> token;
  std::optional<Condition> condition;
};
struct TimingPoint {
  Epoch epoch = 0;
  Id label = 0;
  Tick interval = 0;
  std::vector<Id> manifest;
};
struct Group {
  TimingPoint point;
  std::vector<ReservedEvent> events;
  std::string configuration;
};
struct GroupReply {
  Id label = 0;
};
struct EndOfStream {
  Id last_label = 0;
};
struct Completion {
  Token token;
  bool value = false;
};
struct LaunchBatch {
  Epoch epoch = 0;
  Id label = 0;
  Tick fire_tick = 0;
  std::vector<ReservedEvent> events;
};
```
<!-- /source -->

## Trace record

[include/qsbit/trace.hpp](../include/qsbit/trace.hpp)

<!-- source: {"path": "include/qsbit/trace.hpp", "start": "struct TraceEvent {", "end": "class Trace {"} -->
```cpp
struct TraceEvent {
  TraceEvent(Tick at, Epoch session, std::string type, Id identity = 0, Id timing_label = 0,
             Tick local_cycle = 0)
      : tick(at), epoch(session), kind(std::move(type)), id(identity), label(timing_label),
        cycle(local_cycle) {}
  Tick tick = 0;
  Epoch epoch = 0;
  std::string kind;
  Id id = 0, label = 0;
  Tick cycle = 0;
  std::uint32_t port = 0, codeword = 0;
  std::uint32_t pc = 0, word = 0, rd = 0, next_pc = 0;
  std::vector<std::uint32_t> registers;
  std::vector<std::uint32_t> targets;
  std::string operation, detail;
  std::uint64_t value = 0;
};
```
<!-- /source -->

