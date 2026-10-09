# Simulator component reference

Each component page describes its inputs, outputs, state, errors and tests.
The [implementation map](../implementation.md#implementation-map) shows their
connections. See [simulation timing](../module-architecture.md) for edge order
and communication delays.

## Setup and CPU

| Module | What it does |
| --- | --- |
| [Platform and clocks](platform-and-clocks.md) | Creates the simulation profile, clocks and scheduled wakeups. |
| [ELF loader](elf-loader-and-program-image.md) | Loads executable bytes, segment permissions and entry PC. |
| [ISA decoder](isa-decoder-and-semantics.md) | Decodes instructions and calculates architectural effects. |
| [CPU cycle model](cpu-cycle-model.md) | Advances the pipeline, handles stalls and retires instructions. |
| [Memory](memory-and-response-model.md) | Completes timed fetches, loads and stores. |
| [Quantum instruction adapter](quantum-instruction-adapter.md) | Converts instruction fields and operands into control operations. |

## Command preparation and TCU

| Module | What it does |
| --- | --- |
| [Port and codeword mapping](port-codeword-mapping.md) | Selects device events from the simulation profile. |
| [Codeword decoding](codeword-decoding.md) | Expands commands into identified per-port events. |
| [Reserve phase](reserve-phase.md) | Prepares events at a time point and requests queue insertion. |
| [Communication latency and enqueue](queue-enqueue.md) | Transfers requests and inserts timing and event entries together. |
| [Timing queue](timing-queue.md) | Retains intervals, due cycles and member lists. |
| [Per-port event queues](per-port-event-queues.md) | Retain events until their time point's label triggers. |
| [TCU timer](timing-controller.md) | Selects the due time point on each TCU edge. |
| [Conditional execution checks](conditional-execution.md) | Checks execution flags and validates the selected events. |

## Devices, feedback and completion

| Module | What it does |
| --- | --- |
| [Control output and resource checks](control-output.md) | Reserve and execute physical intervals. |
| [Quantum backend](quantum-device-model.md) | Supplies quantum-state evolution, measurement outcomes, or both. |
| [Acquisition and discrimination](acquisition-and-discrimination.md) | Samples measurements and schedules result readiness. |
| [Measurement result registers](measurement-registers.md) | Stores per-qubit bits and pending measurement counts for FMR. |
| [Execution flags](execution-flags.md) | Updates per-qubit flags used at the trigger edge. |
| [Tracing and completion](tracing-and-completion.md) | Records observations and distinguishes complete drain from failure. |
| [Synchronization](synchronization.md) | Synchronizes neighboring controllers using timed booking signals. |
| [Decoder transport and processing](decoder-transport-and-processing.md) | Transfers syndrome bits, schedules decoding jobs and returns corrections through MMIO. |

Run a component's named tests with `ctest --test-dir BUILD_DIRECTORY -R NAME`.
Numerical tests require the corresponding [build options](../building.md#cmake-options).

```{toctree}
:hidden:

platform-and-clocks
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
port-codeword-mapping
control-output
quantum-device-model
acquisition-and-discrimination
measurement-registers
execution-flags
tracing-and-completion
synchronization
decoder-transport-and-processing
```
