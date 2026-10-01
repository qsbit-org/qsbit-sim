# C++ interface contracts

These interfaces connect the CPU, timing control, TCU and trace consumers.
Their declarations are checked against the current headers.
See the [control protocol](module-architecture.md) for ordering and the
[external formats](interfaces.md) for program and JSON inputs.

## Time and cycle units

`Tick` is a `uint64_t` alias reused for timestamps and cycle counts. The field
contract, not the C++ alias, determines the unit.

| Fields | Unit |
| --- | --- |
| `Clock::period`, `Clock::phase`, `Profile::start`, `Profile::watchdog` | Nanoseconds |
| Mailbox `published` and `eligible`, `TriggeredEvents::fire_tick`, `TraceEvent::tick` | Global simulation ticks, 1 ns each |
| `ScheduledEvent::start`, `ScheduledEvent::end` | Global simulation ticks |
| Action `delay`, `duration`, `discriminator_delay` | Nanoseconds |
| `TimingPoint::interval` | TCU cycles since the preceding timing point |
| TCU `Point::due`, `last_due_`; current time point | Logical TCU cycle in the current epoch |
| `TraceEvent::cycle` | Kind-specific CPU edge index or logical TCU cycle; see [trace fields](interfaces.md#jsonl-trace) |
| `memory_latency` | CPU periods after acceptance |
| Configured crossing latencies | Receiver edges, starting strictly after publication |

## Source compatibility

New code uses `TimingControl`, `MeasurementResults`, `ConditionalResults`,
`ControlElectronics`, `ResourceReservations`, `TimingEvents`, `EnqueueReply`,
`OperationEvent`, `TriggeredEvents`, `ScheduledEvent`, `EventSpec` and
`MeasurementReference`. These are simulator implementation types.

The headers retain the former names as C++ aliases for existing adapters:
`TimelineProducer`, `Scoreboard`, `FastHistory`, `DeviceRuntime`,
`ResourceCalendar`, `Group`, `GroupReply`, `ReservedEvent`, `LaunchBatch`,
`PhysicalAction`, `ActionSpec` and `Token`, respectively.
`ProducerOperation` and `ProducerKind` alias `ControlOperation` and `ControlKind`.
The former accessors and `lower()` and `validate_group()` forward to their
current equivalents. Adapters must be rebuilt; compiled-library ABI stability
is not guaranteed.

Machine encodings, JSON schemas, trace event names, fault diagnostics and timing
remain unchanged. Public data members such as mailbox `eligible` and
`ControlLinks::groups` retain their names for source compatibility.

`control.source_compatibility` compiles existing type names and checks the
forwarding functions and accessors against the current interfaces.

## CPU adapter

`Simulator` calls `step()` once per CPU edge. The model owns its registers,
PC and pipeline state. `CpuPorts::control` returns an optional 32-bit result:
absence means the same operation must remain held; a present value completes it.
Use the CPU factory to supply another implementation.

Source: [include/qsbit/cpu.hpp](../include/qsbit/cpu.hpp).

<!-- source: {"path": "include/qsbit/cpu.hpp", "start": "struct CpuPorts {", "end": "class CpuCycleModel final"} -->
```cpp
struct CpuPorts {
  MemoryPort &fetch;
  MemoryPort &data;
  std::function<std::optional<std::uint32_t>(const ControlOperation &)> control;
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

`step()` checks a due timing point and an optional enqueue candidate at one TCU
edge. The preflight callback validates physical actions before queue removal.
`TcuOutput` reports enqueue, the launch batch and delivered fast-result tokens.
The method computes the logical cycle from its tick and configured start.

Source: [include/qsbit/tcu.hpp](../include/qsbit/tcu.hpp).

<!-- source: {"path": "include/qsbit/tcu.hpp", "start": "struct TcuOutput {", "end": "} // namespace qsbit"} -->
```cpp
struct TcuOutput {
  bool admitted = false;
  std::optional<TriggeredEvents> launch;
  std::vector<MeasurementReference> fast_delivered;
};
class TcuCycleModel {
public:
  using Preflight = std::function<void(const TriggeredEvents &)>;
  TcuCycleModel(const Profile &profile, Trace &trace);
  TcuOutput step(Tick now, Epoch epoch, const TimingEvents *candidate,
                 const std::vector<Completion> &results, const Preflight &preflight);
  void close(const EndOfStream &end);
  void reset(Tick epoch_origin = 0);
  [[nodiscard]] bool drained() const;
  [[nodiscard]] std::size_t timing_size() const { return timing_.size(); }
  [[nodiscard]] std::size_t port_size(std::uint32_t port) const { return events_.at(port).size(); }
  [[nodiscard]] Id last_label() const { return last_label_; }
  [[nodiscard]] Tick last_due() const { return last_due_; }
  [[nodiscard]] const ConditionalResults &history() const { return history_; }

private:
  struct Point {
    TimingPoint point;
    Tick due;
  };
  const Profile &profile_;
  Trace &trace_;
  std::deque<Point> timing_;
  std::vector<std::deque<OperationEvent>> events_;
  ConditionalResults history_;
  Tick last_due_ = 0;
  Tick start_ = 0;
  Id last_label_ = 0;
  bool closed_ = false;
};
```
<!-- /source -->

## Control records

`TimingEvents` combines one timing point and its resolved events. The manifest lists
exact event IDs; `configuration` is the profile fingerprint. Per-port counts
are computed from the event list.

`EnqueueReply` acknowledges a label. `EndOfStream` identifies the last enqueued
label, with zero for an empty stream. Mailbox envelopes supply epoch and
visibility timing; the simulator has one control stream.

Source: [include/qsbit/control.hpp](../include/qsbit/control.hpp).

<!-- source: {"path": "include/qsbit/control.hpp", "start": "struct MeasurementReference {", "end": "struct ScheduledEvent {"} -->
```cpp
struct MeasurementReference {
  Epoch epoch = 0;
  Id measurement = 0;
  std::uint32_t slot = 0, generation = 0, handle = 0, target = 0;
  bool operator==(const MeasurementReference &) const = default;
};
struct Condition {
  MeasurementReference token;
  bool expected = false;
};
struct OperationEvent {
  Epoch epoch = 0;
  Id id = 0, instruction = 0, label = 0;
  std::uint32_t source_port = 0, codeword = 0;
  EventSpec action;
  std::optional<MeasurementReference> token;
  std::optional<Condition> condition;
};
struct TimingPoint {
  Epoch epoch = 0;
  Id label = 0;
  Tick interval = 0;
  std::vector<Id> manifest;
};
// One enqueue request: a timing point and its associated operation events.
struct TimingEvents {
  TimingPoint point;
  std::vector<OperationEvent> events;
  std::string configuration;
};
struct EnqueueReply {
  Id label = 0;
};
struct EndOfStream {
  Id last_label = 0;
};
struct Completion {
  MeasurementReference token;
  bool value = false;
};
struct TriggeredEvents {
  Epoch epoch = 0;
  Id label = 0;
  Tick fire_tick = 0;
  std::vector<OperationEvent> events;
};
```
<!-- /source -->

## Trace record

`TraceEvent` stores a timestamp, kind and event-specific fields.
The kind determines the meaning of `id`, `cycle` and `value`; there are no
separate clock-domain or status fields. JSONL serialization adds `schema: 1`.

Source: [include/qsbit/trace.hpp](../include/qsbit/trace.hpp).

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
