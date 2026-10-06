# C++ interface contracts

The CPU, TCU and control records below connect the simulator's components.
See [simulation timing](module-architecture.md) for call order and
[file formats](interfaces.md) for program and JSON inputs.

## Time and cycle units

`Tick` is a `uint64_t` alias reused for timestamps and cycle counts. The field
contract, not the C++ alias, determines the unit.

| Fields | Unit |
| --- | --- |
| `Clock::period`, `Clock::phase`, `Profile::start`, `Profile::watchdog` | Nanoseconds |
| Mailbox `published` and `eligible`, `TriggeredEvents::fire_tick`, `TraceEvent::tick` | Global simulation ticks, 1 ns each |
| `ScheduledEvent::start`, `ScheduledEvent::end` | Global simulation ticks |
| Action `delay`, `duration`, `discriminator_delay` | Nanoseconds |
| `TimingPoint::interval` | TCU cycles since the preceding time point |
| TCU `Point::due`, `last_due_`; current time point | Logical TCU cycle in the current epoch |
| `TraceEvent::cycle` | Kind-specific CPU edge index or logical TCU cycle; see [trace fields](interfaces.md#jsonl-trace) |
| `memory_latency` | CPU periods after acceptance |
| Configured crossing latencies | Receiver edges, starting strictly after publication |

## CPU adapter

`Core` calls `step()` once per CPU edge. The model owns its registers,
PC and pipeline state. `CpuPorts::control` returns an optional 32-bit result:
absence means the same operation must remain held; a present value completes it.
Use the CPU factory to supply another implementation.

Source: [include/qsbit/cpu.hpp](../include/qsbit/cpu.hpp).

<!-- source: {"path": "include/qsbit/cpu.hpp", "start": "struct CpuPorts {", "end": "} // namespace qsbit"} -->
```cpp
struct CpuPorts {
  MemoryPort &fetch;
  MemoryPort &data;
  std::function<std::optional<std::uint32_t>(const ControlOperation &)> control;
};
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

`step()` checks a due time point and an optional enqueue candidate at one TCU
edge. The `preflight` callback validates physical events before queue removal.
`TcuOutput` reports queue insertion, triggered events and delivered
measurement references.
The method computes the logical cycle from its tick and configured start.

Source: [include/qsbit/tcu.hpp](../include/qsbit/tcu.hpp).

<!-- source: {"path": "include/qsbit/tcu.hpp", "start": "struct TcuOutput {", "end": "} // namespace qsbit"} -->
```cpp
struct TcuOutput {
  bool admitted = false;
  std::optional<TriggeredEvents> launch;
  std::vector<MeasurementReference> fast_delivered;
  std::vector<std::uint32_t> synchronizations;
};
class TcuCycleModel {
public:
  using Preflight = std::function<void(const TriggeredEvents &)>;
  using SyncPreflight = std::function<void(std::span<const std::uint32_t>)>;
  TcuCycleModel(const Profile &profile, Trace &trace, std::size_t sync_capacity = 8);
  TcuOutput step(Tick now, Epoch epoch, const TimingEvents *candidate,
                 const std::vector<Completion> &results, const Preflight &preflight,
                 bool paused = false, const SyncPreflight &sync_preflight = {});
  void close(const EndOfStream &end);
  void reset(Tick epoch_origin = 0);
  [[nodiscard]] bool drained() const;
  [[nodiscard]] std::size_t timing_size() const { return timing_.size(); }
  [[nodiscard]] std::size_t port_size(std::uint32_t port) const { return events_.at(port).size(); }
  [[nodiscard]] Id last_label() const { return last_label_; }
  [[nodiscard]] Tick last_due() const { return last_due_; }
  [[nodiscard]] const ExecutionFlags &execution_flags() const { return execution_flags_; }

private:
  struct Point {
    TimingPoint point;
    Tick due;
  };
  const Profile &profile_;
  Trace &trace_;
  std::deque<Point> timing_;
  std::vector<std::deque<OperationEvent>> events_;
  ExecutionFlags execution_flags_;
  Tick last_due_ = 0;
  Tick start_ = 0;
  Tick paused_ticks_ = 0;
  std::size_t sync_capacity_ = 8, sync_size_ = 0;
  Id last_label_ = 0;
  bool closed_ = false;
};
```
<!-- /source -->

## Control records

`TimingEvents` combines one time point and its resolved events. The manifest lists
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
  std::uint32_t target = 0;
  bool operator==(const MeasurementReference &) const = default;
};
struct OperationEvent {
  Epoch epoch = 0;
  Id id = 0, instruction = 0, label = 0;
  std::uint32_t source_port = 0, codeword = 0;
  EventSpec action;
  std::optional<MeasurementReference> reference;
  std::optional<std::uint32_t> core = {};
};
struct TimingPoint {
  Epoch epoch = 0;
  Id label = 0;
  Tick interval = 0;
  std::vector<Id> manifest;
  std::vector<std::uint32_t> synchronizations = {};
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
  MeasurementReference reference;
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

## Backend execution

`ControlElectronics` commits time-ordered operations to `BackendExecution`.
`BackendExecution` owns each operation's events and measurement references until execution.
`IQuantumBackend::execute()` consumes the complete batch synchronously and returns
one bit per reference in its terminal measurement, or an empty vector otherwise.
See [backend execution](backends.md#backend-execution) for batch limits and reset behavior.

Source: [include/qsbit/backend.hpp](../include/qsbit/backend.hpp).

<!-- source: {"path": "include/qsbit/backend.hpp", "start": "struct BackendActivity {", "end": "class BackendExecution {"} -->
```cpp
struct BackendActivity {
  Id id;
  Tick start, end;
  EventSpec action;
  std::optional<MeasurementReference> reference;
};
struct BackendEvolution {
  Tick start, tick;
  std::vector<BackendActivity> drives;
  std::vector<BackendActivity> acquisitions;
};
struct BackendGates {
  Tick tick;
  std::vector<EventSpec> gates;
};
struct BackendMeasurement {
  Tick tick;
  std::vector<MeasurementReference> references;
};
using BackendOperation = std::variant<BackendEvolution, BackendGates, BackendMeasurement>;
struct BackendExecutionConfig {
  std::uint32_t max_batch_operations = 1024;
};
class IQuantumBackend {
public:
  virtual ~IQuantumBackend() = default;
  virtual void validate(const EventSpec &action) const = 0;
  virtual void reset(std::uint32_t qubits, std::uint32_t seed) = 0;
  virtual std::vector<bool> execute(Epoch epoch, std::span<const BackendOperation> operations) = 0;
  [[nodiscard]] virtual std::vector<std::complex<double>> state() const { return {}; }
  [[nodiscard]] virtual std::vector<std::vector<std::complex<double>>> density_matrix() const {
    return {};
  }
};
```
<!-- /source -->

## Decoder configuration

`DecoderSystemConfig` supplies bounded transport parameters and numerical
callbacks. Each callback accepts one measurement window and returns correction
bits. See [decoder feedback](decoding.md) for timing and register semantics.

<!-- source: {"path": "include/qsbit/decoder.hpp", "start": "struct DecoderConfig {", "end": "class DecoderSystem {"} -->
```cpp
struct DecoderConfig {
  std::uint32_t id = 0, measurements = 0, outputs = 0;
  Tick latency = 1, initiation_interval = 1;
  std::function<std::vector<bool>(std::span<const std::uint8_t>)> decode;
};
struct DecoderSystemConfig {
  std::uint32_t base = 0x40000000, request_capacity = 16, result_capacity = 16;
  Tick link_latency = 1;
  std::uint32_t bytes_per_tick = 1, packet_overhead = 16;
  std::vector<DecoderConfig> decoders;
};
```
<!-- /source -->

## Trace record

`TraceEvent` stores a timestamp, kind and event-specific fields.
The kind determines the meaning of `id`, `cycle` and `value`. JSONL serialization adds `schema: 1`.

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
  std::optional<std::uint32_t> core;
  std::optional<CpuPipelineState> pipeline;
};
```
<!-- /source -->
