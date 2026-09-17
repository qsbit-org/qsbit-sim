# Module reference

Use these pages to look up a module's connections, behavior, stored state and
tests. Start with the [architecture overview](../high-level-design.md) for the
complete data path, or select a module in the
[architecture diagram](../architecture.md).

The names describe logical responsibilities. `Simulator` schedules C++ owners;
the boxes do not each represent a separate SystemC process or clock stage.
The [control protocol](../module-architecture.md) defines the shared timing rules.

## Setup and CPU

| Module | What it does |
| --- | --- |
| [Platform and clocks](platform-and-clock-adapter.md) | Creates the run profile, clocks and scheduled wakeups. |
| [ELF loader](elf-loader-and-program-image.md) | Loads executable bytes, segment permissions and entry PC. |
| [ISA decoder](isa-decoder-and-semantics.md) | Decodes instructions and calculates architectural effects. |
| [CPU cycle model](cpu-cycle-model.md) | Advances the pipeline, handles stalls and retires instructions. |
| [Memory](memory-and-response-model.md) | Completes timed fetches, loads and stores. |
| [Quantum instruction adapter](quantum-instruction-adapter.md) | Converts instruction fields and operands into producer operations. |

## Command preparation and TCU

| Module | What it does |
| --- | --- |
| [Port/codeword map](port-codeword-and-waveform-map.md) | Selects device actions from the run profile. |
| [Operation lowerer](operation-lowerer-and-device-distributor.md) | Expands commands into identified per-port events. |
| [Timeline producer](timeline-reservation-manager.md) | Collects actions for a planned cycle and seals groups. |
| [Crossing and admission](command-crossing-and-admission.md) | Transfers groups and inserts all their queue entries together. |
| [Timing queue](timing-queue.md) | Retains intervals, due cycles and member lists. |
| [Per-port event queues](per-port-event-queues.md) | Retain actions until their group's label fires. |
| [TCU timer](tcu-timer-and-label-broadcaster.md) | Selects the due group on each TCU edge. |
| [Conditions and launch checks](condition-gate-and-launch-preflight.md) | Filters conditional actions and checks the complete launch. |

## Devices, feedback and completion

| Module | What it does |
| --- | --- |
| [Output channels and resource calendar](output-channels-and-resource-calendar.md) | Reserve and execute physical intervals. |
| [Quantum backend](quantum-state-service-and-backends.md) | Evolves shared state and returns measurement outcomes. |
| [Acquisition and discrimination](acquisition-and-discrimination.md) | Samples measurements and schedules result readiness. |
| [Scoreboard and CPU feedback](measurement-scoreboard-and-cpu-feedback.md) | Track handles and deliver results to QREAD. |
| [Fast-condition history](fast-condition-history.md) | Retains exact-token results for conditional TCU output. |
| [Trace and stop](trace-recorder-and-stop-controller.md) | Record observations and distinguish complete drain from failure. |
| [Synchronization boundary](future-synchronization-adapter.md) | Rejects QSYNC; distributed synchronization is not implemented. |

In module diagrams, solid arrows show inputs and outputs; dashed arrows connect
state or configuration to the behavior that uses it. Each **CTest** entry names
registered tests. Optional numerical tests are identified separately.

```{toctree}
:hidden:

platform-and-clock-adapter
elf-loader-and-program-image
isa-decoder-and-semantics
cpu-cycle-model
memory-and-response-model
quantum-instruction-adapter
operation-lowerer-and-device-distributor
timeline-reservation-manager
command-crossing-and-admission
timing-queue
per-port-event-queues
tcu-timer-and-label-broadcaster
condition-gate-and-launch-preflight
port-codeword-and-waveform-map
output-channels-and-resource-calendar
quantum-state-service-and-backends
acquisition-and-discrimination
measurement-scoreboard-and-cpu-feedback
fast-condition-history
trace-recorder-and-stop-controller
future-synchronization-adapter
```
