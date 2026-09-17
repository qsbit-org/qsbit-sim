# Implementation and default profile

This reference lists the current libraries, numerical defaults and supported
features. The [architecture overview](high-level-design.md) explains the data
path; the [control protocol](module-architecture.md) defines event ordering.

## Libraries and owners

| Library | Contents | Dependencies |
| --- | --- | --- |
| `qsbit_core` | ISA, memory, CPU interface and model, producer, TCU, feedback and devices. | C++20; no SystemC or Python link dependency. |
| `qsbit_systemc` | `Simulator` clocks, timed wakeups, reset and device barrier. | Core and SystemC. |
| `qsbit_config` | JSON run and profile parsing. | Core and the configured JSON library. |
| `qsbit_python` | Calls to optional Python backend adapters. | Python development library and pybind11; built only when enabled. |

The [module reference](modules/README.md) maps logical responsibilities to their
C++ owners and source files.

`Simulator` registers three clock methods: CPU and memory on CPU rising edges,
and TCU on TCU rising edges. A timed wakeup handles device boundaries, reset and
watchdog. A zero-time barrier processes physical work only after every clocked
method due at that tick has finished.

## Default timing

The time resolution is 1 ns. Protocol times, epochs and IDs use checked 64-bit
integers. Architectural registers and addresses use 32 bits.

| Setting | Default |
| --- | --- |
| CPU and memory period | 5 ns |
| TCU period | 20 ns |
| CPU and TCU phase | 0 ns |
| Initial TCU cycle zero | 1000 ns |
| Command, reply and CPU-result crossing | 1 receiver edge |
| Fast-result crossing | 2 TCU receiver edges |
| Memory service | 1 CPU period after acceptance |
| Timing queue capacity | 32 points |
| Event queue capacity | 32 entries per port |
| Producer staging | 16 actions |
| Measurement slots | 8 |
| Firing width | 1 action per port per group |

The example run files override TCU start to 200 ns. These are configurable
values, not fixed hardware constants. Dump the complete current
profile, including mappings and other settings, with:

```sh
build-gcc/qsbit-sim --dump-default-profile out/default-profile.json
```

Each run summary records its full validated profile and an FNV-1a fingerprint.
The fingerprint identifies the configuration; it is not a cryptographic check.
The profile stays fixed throughout the run and all session resets.

## CPU and memory

The default CPU is a single-issue, in-order pipeline with fetch, decode and
execute/commit stages. Each latch advances at most once per CPU edge.
An older instruction retires before a younger instruction entering execute
captures its operands.

A load or blocked extension holds execute. A taken branch discards younger
instructions and invalidates pending fetch generations. Only the oldest
instruction can publish a store or producer operation. Speculative fetch faults
become fatal only when their instruction is oldest.

Memory has separate fetch and data ports, each with one pending transaction.
Request crossing, service time and response crossing are distinct delays.
Stores occur once at completion. Reset cancels pending transactions and
preserves committed bytes.

## Producer and TCU

QAPPEND resolves a mapping and stages actions for the current producer cursor.
It completes on local acceptance. QADVANCE, QFLUSH, QREAD and QEND seal the open
group when required; only one submission can await an admission reply.
QADVANCE(0) changes neither cursor nor group.

TCU admission inserts the timing point and all event members together.
It checks capacity before firing removes any old entries. The timer selects
due groups using cumulative intervals, including across empty-queue gaps.
Conditions use exact-token history from earlier TCU edges.

The TCU validates the entire transition before committing firing and admission.
A false condition consumes its event with a cancellation record. It does not
shift subsequent points.

## Device actions

Mappings select `gate`, `pulse`, `acquire` or `arm` actions.
Physical start is `fire_tick + delay`. All durations are positive, and resources
are occupied over `[start, end)`.

At each boundary, the runtime evolves the previous drive set, samples ending
acquisitions, ends old actions, applies starting gates and activates new
intervals. Ready results are published last. Overlapping permitted pulses are
evolved jointly. A gate starting on the same target and tick as a measurement
sample is unsupported.

Readiness is `max(acquisition_end, arm_start) + discriminator_delay`.
An implicit arm uses acquisition start. The discriminator delay may be zero;
feedback still crosses to a strictly later receiver edge.

## Backend limits

| Backend | Supported behavior | Limits |
| --- | --- | --- |
| Scripted | Fixed measurement bits indexed by measurement ID. | No quantum state. |
| Aer | Persistent statevector, supported one- and two-qubit gates, joint measurement and collapse. | 1–20 qubits; no pulse integration or noise model. |
| Pulse | Aer gates and measurement, plus joint constant X, Y and Z Hamiltonian evolution with SciPy `expm`. | 1–8 qubits; no sampled waveforms or dissipative solver. |

Pulse amplitude is angular frequency in radians/ns with
`H = sum(amplitude * Pauli / 2)` and hbar = 1.
Rotation-gate amplitude is an angle in radians.
Qubit 0 is the least significant statevector bit.

## Unsupported features

The current profile does not implement privileged execution, interrupts, caches,
compressed instructions, distributed synchronization, TQEC input, sampled
waveforms, dissipation or GPU adapters. ECALL and EBREAK raise distinct traps;
FENCE.I and unselected ISA extensions raise `IllegalInstruction`.
QSYNC raises `UnsupportedSynchronization`.

Session reset clears controller and quantum state under the same profile.
A controller-only reset that preserves qubit state is not implemented.

See [ADR 0001](decisions/0001-initial-implementation.md) for the implementation
choices and [ADR 0002](decisions/0002-reference-comparison-scope.md) for the scope
of CACTUS timing comparisons.
