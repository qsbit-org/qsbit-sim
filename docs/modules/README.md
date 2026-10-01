# Simulator component reference

Use these pages to look up a module's connections, behavior, stored state and
tests. Start with the [architecture overview](../high-level-design.md) for the
complete data path, or select a module in the
[architecture diagram](../architecture.md).

These pages document C++ responsibilities, stored state and simulation checks.
The [implementation map](../implementation.md#implementation-map) connects all
components. The controller diagram contains the architectural control path;
software helpers and numerical adapters belong to this implementation reference.
The [timing contract](../module-architecture.md) defines shared ordering rules.

## Setup and CPU

| Module | What it does |
| --- | --- |
| [Platform and clocks](platform-and-clock-adapter.md) | Creates the simulation profile, clocks and scheduled wakeups. |
| [ELF loader](elf-loader-and-program-image.md) | Loads executable bytes, segment permissions and entry PC. |
| [ISA decoder](isa-decoder-and-semantics.md) | Decodes instructions and calculates architectural effects. |
| [CPU cycle model](cpu-cycle-model.md) | Advances the pipeline, handles stalls and retires instructions. |
| [Memory](memory-and-response-model.md) | Completes timed fetches, loads and stores. |
| [Quantum instruction adapter](quantum-instruction-adapter.md) | Converts instruction fields and operands into control operations. |

## Command preparation and TCU

| Module | What it does |
| --- | --- |
| [Port-codeword action map](port-codeword-and-waveform-map.md) | Selects device actions from the simulation profile. |
| [Codeword decoding](codeword-decoding.md) | Expands commands into identified per-port events. |
| [Reserve phase](reserve-phase.md) | Prepares events at a time point and requests queue insertion. |
| [Communication latency and enqueue](queue-enqueue.md) | Transfers requests and inserts timing and event entries together. |
| [Timing queue](timing-queue.md) | Retains intervals, due cycles and member lists. |
| [Per-port event queues](per-port-event-queues.md) | Retain actions until their timing point's label triggers. |
| [TCU timer](timing-controller.md) | Selects the due timing point on each TCU edge. |
| [Conditional execution checks](conditional-execution.md) | Tests measurement conditions and validates the selected events. |

## Devices, feedback and completion

| Module | What it does |
| --- | --- |
| [Output channels and simulation resource checks](control-output.md) | Reserve and execute physical intervals. |
| [Quantum backend](quantum-device-model.md) | Evolves shared state and returns measurement outcomes. |
| [Acquisition and discrimination](acquisition-and-discrimination.md) | Samples measurements and schedules result readiness. |
| [Measurement results and CPU feedback](measurement-results.md) | Tracks individual measurements and delivers results to QREAD. |
| [Results for conditional execution](conditional-results.md) | Retains individual results for conditional TCU output. |
| [Trace and stop](trace-recorder-and-stop-controller.md) | Record observations and distinguish complete drain from failure. |
| [Synchronization boundary](future-synchronization-adapter.md) | Rejects QSYNC; distributed synchronization is not implemented. |

In all architecture diagrams, solid arrows carry values or calls. Dashed arrows
show labeled dependencies: scheduling, observation, state or configuration. Each **CTest** entry names
registered tests. Optional numerical tests are identified separately.

```{toctree}
:hidden:

platform-and-clock-adapter
elf-loader-and-program-image
isa-decoder-and-semantics
cpu-cycle-model
memory-and-response-model
quantum-instruction-adapter
codeword-decoding
reserve-phase
queue-enqueue
timing-queue
per-port-event-queues
timing-controller
conditional-execution
port-codeword-and-waveform-map
control-output
quantum-device-model
acquisition-and-discrimination
measurement-results
conditional-results
trace-recorder-and-stop-controller
future-synchronization-adapter
```
