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
| `qsbit::contracts` | Controller instruction encodings, decoder registers, operation names and executable layout. | C++20 headers. |
| `qsbit_model_types` | Time, profiles, protocol records and traces. | Contracts. |
| `qsbit_controller` | ISA, memory, CPU pipeline, TCU, synchronization and feedback. | Model types. |
| `qsbit_device` | Output scheduling, resource reservations and backend execution. | Model types. |
| `qsbit_platform` | Core composition and decoder transport. | Controller. |
| `qsbit_systemc` | `Simulator` clocks, timed wakeups, reset and device barrier. | Platform, device and SystemC. |
| `qsbit_config` | Profile JSON parsing and serialization. | Model types and the configured JSON library. |
| `qsbit_app` | Run-file and CLI parsing, program loading and core configuration. | Platform, device and profile JSON support. |
| `qsbit_python` | Calls to optional Python backend adapters. | Python development headers and library, plus pybind11; built only when enabled. |

The executable and `qsbit::contracts` headers are installed. The simulator's
other headers describe internal C++ interfaces and are not installed.

Direct include dependencies are checked against `architecture.json`.
Controller code cannot include device implementation headers. Standalone header
compilation checks each header with its owning target's include paths.

Configuration parsing resolves program paths, core profiles and synchronization
connections before component construction. The application retains the parsed configuration
as a const value. Backend options remain opaque JSON; normalized backend options
are stored separately for the run summary.
`Core` derives `TransportConfig`, `TimingConfig` and `TcuConfig` from its validated
profile. Each component owns its configuration; `TimingConfig` indexes codeword
mappings by source port and codeword. `ControlLinks` owns the bounded mailboxes and depends
on protocol records and transport settings, not on `TimingControl`.

The [module reference](modules/README.md) maps logical responsibilities to their
C++ owners and source files.

Each `Core` owns its CPU, memory, timing control, TCU, measurement registers
and synchronization unit. All cores submit events to one `ControlElectronics`
instance and share one backend. `Simulator` owns SystemC scheduling, described
in [simulation time and execution](simulation-model.md).

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

The Bell-state and measurement-feedback example configurations set the TCU
start to 200 ns. To inspect all defaults:

```sh
qsbit-sim --dump-default-profile out/default-profile.json
```

Each run summary includes the validated profile and its FNV-1a fingerprint.
`TimingEvents.configuration` carries this string. A summary instead uses
`configuration_hash` for the fingerprint and `configuration` for the full profile.
The profile stays fixed throughout the run and all session resets.

## Controller and device components

Both CPU models use `InOrderPipeline`; `ControlIssue` selects scalar or bundled
codeword submission. [CPU execution](modules/cpu-cycle-model.md) describes stage
progress, operand capture and speculative faults.
[Memory](modules/memory-and-response-model.md) owns separate fetch and data
transactions.

`TimingControl` stages mapped events and retains enqueue requests until
acknowledgment. `TcuCycleModel` owns the timing queue, per-port event queues,
timer and execution flags. See [reserve phase](modules/reserve-phase.md) and
[TCU state](modules/timing-controller.md).

`ControlElectronics` schedules physical intervals and submits backend operations.
`ResourceReservations` checks conflicts, gate-output resolution pairs the
configured endpoints, and `Readouts` owns acquisition pairing, readiness and
sampled results. The [timing reference](module-architecture.md#device-batches-and-feedback)
defines their processing order.

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
