# Implementation and default profile

The simulator separates clocked controller models from device execution and
quantum-state evolution.

## Implementation map

The implementation connects these C++ components:

```{graphviz} implementation.dot
:alt: Simulator implementation objects, their owned state and communication paths.
```

Solid arrows carry data or calls. Dashed arrows show scheduling and observation.

## Libraries and owners

| Library | Contents | Dependencies |
| --- | --- | --- |
| `qsbit_core` | ISA, memory, CPU models, Core, TCU, synchronization, feedback and devices. | C++20; no SystemC or Python link dependency. |
| `qsbit_systemc` | `Simulator` clocks, timed wakeups, reset and device barrier. | Core and SystemC. |
| `qsbit_config` | JSON run and profile parsing. | Core and the configured JSON library. |
| `qsbit_python` | Calls to optional Python backend adapters. | Python development headers and library, plus pybind11; built only when enabled. |

The [module reference](modules/README.md) maps logical responsibilities to their
C++ owners and source files.

`Simulator` registers three clock methods: CPU and memory on CPU rising edges,
and TCU on TCU rising edges. A timed wakeup handles device and decoder boundaries,
reset and watchdog. A zero-time barrier processes physical work only after every clocked
method due at that tick has finished. Each `Core` has independent CPU and memory
state. All cores submit outputs to one `ControlElectronics` instance and share
one backend. Neighbor synchronization uses BISP, the booking-based synchronization
protocol from Distributed-HISQ. See [distributed simulation](distributed-simulation.md).

## Default timing

The time resolution is 1 ns. Clock phase locates rising edges relative to global
time zero; TCU start selects the initial logical-cycle origin. See
[time and cycle units](cpp-interfaces.md#time-and-cycle-units).
Protocol times, epochs and IDs use checked 64-bit
integers. Architectural registers and addresses use 32 bits.

| Setting | Default |
| --- | --- |
| CPU and memory period | 5 ns |
| TCU period | 20 ns |
| CPU and TCU phase | 0 ns |
| Initial TCU cycle zero | 1000 ns |
| Command, reply and CPU-result mailbox communication latency | 1 receiver edge |
| Fast-result mailbox communication latency | 2 TCU receiver edges |
| Memory service | 1 CPU period after acceptance |
| Timing queue capacity | 32 points |
| Event queue capacity | 32 entries per port |
| Timing control staging | 16 events |
| Outstanding measurements per delivery path | 8 |
| Output width | 1 event per port per time point |

The example run files set TCU start to 200 ns. To inspect all defaults:

```sh
build-clang/qsbit-sim --dump-default-profile out/default-profile.json
```

Each run summary records its full validated profile and an FNV-1a fingerprint.
The fingerprint identifies the configuration.
`TimingEvents.configuration` carries this string. A summary instead uses
`configuration_hash` for the fingerprint and `configuration` for the full profile.
The profile stays fixed throughout the run and all session resets.

## CPU and memory

The default CPU is a single-issue, in-order pipeline with fetch, decode and
execute stages; execution also commits results. The
[CPU glossary](glossary.md#programs-and-cpu-execution) introduces the model.
Each latch advances at most once per CPU edge.
An older instruction retires before a younger instruction entering execute
captures its operands.

A load or blocked extension holds execute. A taken branch discards younger
instructions and invalidates pending fetch generations. Only the oldest
instruction can publish a store or control operation. Speculative fetch faults
become fatal only when their instruction is oldest.

Memory has separate fetch and data ports, each with one pending transaction.
Request mailbox communication latency, service time and response mailbox
communication latency are distinct delays.
Stores occur once at completion. Reset cancels pending transactions and
preserves committed bytes.

## Timing control and TCU

`cw` resolves a mapping and prepares events for the current time point.
It completes on local acceptance. Positive `wait`, `fmr` and the exit ECALL
enqueue pending events; only one request can await a reply.
`wait` with interval zero changes neither the time point nor its events.

TCU enqueue inserts the time point and all event members together.
It checks capacity before triggering removes any old entries. The timer selects
due time points using cumulative intervals, including across empty-queue gaps.
Conditional operations use their target qubit's execution flags from earlier TCU edges.

The TCU validates the entire transition before committing triggering and enqueue.
A false condition consumes its event with a cancellation record. It does not
shift subsequent points.

## Device events

Mappings select `gate`, `gate_output`, `pulse`, `acquire` or `arm` events.
Physical start is `fire_tick + delay`. All durations are positive, and resources
are occupied over `[start, end)`.

Two matching `gate_output` events must start together. The shared device checks
both inputs before committing the configured gate once.

At each device event tick, `ControlElectronics` commits evolution over the preceding interval
under the active drives, samples ending acquisitions, removes ended events,
commits starting gates and activates new
intervals. Ready results are published last. Overlapping permitted pulses are
evolved jointly. A gate starting on the same target and tick as a measurement
sample is unsupported.

Readiness is `max(acquisition_end, arm_start) + discriminator_delay`.
An implicit arm uses acquisition start. The discriminator delay may be zero;
feedback still crosses to a strictly later receiver edge.

## Backend limits

| Backend | Supported behavior | Limits |
| --- | --- | --- |
| Mock | Fixed measurement bits indexed by measurement ID. | No quantum state. |
| Aer | Persistent statevector or density matrix, ideal gates, optional thermal relaxation and measurement collapse. | 1–20 qubits in statevector mode; 1–10 in density-matrix mode; no pulse integration. |
| QuTiP | Time-dependent oscillator Hamiltonians, exchange couplings, Lindblad dissipation and projective measurement with optional IQ assignment. | Configured Hilbert-space dimension limit; no ideal gates or continuous quantum readout model. |
| Stim | Clifford gates, optional gate depolarization and measurement collapse. | No thermal relaxation, non-Clifford gates, pulse integration or dense state output. |

Pulse amplitude is angular frequency in radians/ns. [QuTiP pulse models](qutip.md)
defines the drive operators and waveform parameters with hbar = 1.
Rotation-gate amplitude is an angle in radians.
Qubit 0 is the least significant statevector bit.

## Unsupported features

The simulator does not implement privileged execution, interrupts, caches,
compressed instructions, regional synchronization, inter-controller `send` and
`recv` messaging, or TQEC input. No GPU backend is bundled with qsbit-sim.
The exit ECALL completes the program;
other ECALLs and EBREAK raise traps. FENCE.I and unselected ISA extensions
raise `IllegalInstruction`.
`sync` requires a configured neighbor connection; an unconnected target raises
`UnsupportedSynchronization`.

Session reset clears all cores, the synchronization network, the decoder system
and quantum state while preserving the profiles.
A controller-only reset that preserves qubit state is not implemented.

See [ADR 0001](decisions/0001-initial-implementation.md) for the implementation
choices.
