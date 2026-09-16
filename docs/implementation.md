# Executable implementation

Version 0.1.0 implements the Phase 1 modules in C++20 and SystemC 3.0.1. The
[initial profile decision](decisions/0001-initial-implementation.md) fixes the CPU,
instruction encodings and default timing. [Reference comparison scope](decisions/0002-reference-comparison-scope.md)
defines which externally observable times can be compared across different CPUs.

## Build and ownership

`qsbit_core` contains ISA semantics, program memory, the replaceable CPU interface,
producer, TCU, feedback and physical-device transitions. It has no SystemC or Python
link dependency. `qsbit_systemc` wraps these owners in one `Simulator` module;
`qsbit_python` bridges an optional numerical backend, and `qsbit_config` parses profiles.
No network fetch occurs during CMake configure or build.

`Simulator` registers CPU, memory and TCU `SC_METHOD` processes at their positive clock
edges. A timed wakeup covers physical boundaries, reset and watchdog. A delta notification
requests a barrier, which checks that all owners whose clocks have an edge at that tick
have finished. Only then may DeviceRuntime advance shared quantum state. Reversing process
registration changes neither committed messages nor the observable trace. Delta cycles
coordinate execution; they never represent an extra hardware cycle.

Logical module ownership and source locations are listed in [modules/README.md](modules/README.md).
Queues and pure logical substages are intentionally not separate SystemC processes.

## CPU profile

The three stages are fetch, decode and execute/commit, with one pending fetch and one
pending data transaction. Fetch, decode and execute latches advance at most one stage
per edge. Execute reads operands as it enters, after the older same-edge retirement.
A load or blocked extension holds execute; younger stages cannot publish side effects.
Taken branches invalidate younger work, including outstanding fetch generations.
A memory fault becomes fatal only when its instruction is oldest. Stores occur exactly
once on memory completion; reset preserves committed bytes.

Requests and responses each cross on a strictly later CPU edge. Configured memory
latency starts at request acceptance. Consequently a one-cycle memory service does not
mean one cycle from CPU request to CPU response. The ISA library describes effects,
not these delays. `ICpuCycleModel` is the replacement boundary; an external CPU adapter
must preserve oldest-only publication and completion semantics to claim timing support.

## Time and reset

A tick is one nanosecond. Ticks, labels, event IDs and epochs use checked 64-bit values.
The default CPU period is 5 ticks, TCU period 20, and logical TCU cycle zero is tick 1000.
Clock phases are independently configurable. Publication at p becomes visible on the
first receiver edge strictly after p, then N-1 further receiver edges.

Session reset is global and dominates all other work at its tick. It clears protocol
state and quantum state, resets PC to the ELF entry and preserves memory bytes. Its
new logical TCU origin is the first TCU edge at or after `reset_tick + profile.start`.
This makes `start` the startup offset for each epoch. The initial epoch starts at tick
zero. Reset never rewinds SystemC time. Measurement-slot generations survive reset;
old-epoch completions cannot fill a new slot.

The profile is validated and copied before elaboration. CLI overrides create a new
profile; runtime code receives const references. It includes mappings, capacities,
periods, delays, seed and watchdog. Each summary records the complete profile plus its
stable FNV-1a fingerprint; this identifier is not a cryptographic integrity check.

## Producer and TCU

QAPPEND resolves an immutable action mapping and adds actions to bounded staging.
It retires on local acceptance. A measurement reserves a scoreboard token first.
Positive QADVANCE, QFLUSH, QREAD and QEND seal an open group when needed; only one
immutable submission may await acknowledgment. QADVANCE(0) preserves the open group.
QREAD flushes before waiting and consumes a result handle. QEND closes production;
successful stop waits for all queues, physical actions and enabled result paths.

A full CPU result-slot array is a capacity fault: stalling would prevent a later QREAD
from freeing it. Exhausted fast-delivery credits can stall because already-issued work
can release them independently. Oversized groups and per-port width violations fault
before changing staging.

TCU admission appends one timing point and every manifested per-port event atomically.
Credits use old occupancy. Newly admitted groups cannot fire on their admission edge.
Cumulative intervals determine due cycles even across empty-queue gaps. The due label
selects the complete batch, conditions use previously visible exact-token history,
and resource preflight precedes dequeue or output. Fast results arriving on the firing
edge become available only for later edges. A false condition consumes its event with
a cancellation record; it does not change the schedule of later points.

## Physical actions and backends

Mappings select `gate`, `pulse`, `acquire` or `arm` actions. Start is label-fire tick plus
mapping delay. Positive durations reserve half-open port and resource intervals.
Independent ports may overlap; same-target overlapping pulses are summed by the pulse
backend when resource declarations permit them. A gate at a measurement sample tick on
the same target is rejected before backend mutation.

DeviceRuntime evolves the previous drive set to a boundary once, samples ending
acquisitions together, removes ended actions, applies independent starting gates as a
batch, starts new actions, then publishes ready results. With acquisition end E, arm
start A and discriminator latency L, readiness is `max(E,A)+L`. L may be zero; crossing
latencies remain positive. An implicit arm uses acquisition start. A separate arm must
share the acquisition token and target. Readiness arithmetic is validated before any
calendar or boundary mutation.

`IQuantumBackend` supports validation, reset, joint interval evolution, gate batches,
joint measurement with collapse and state inspection. CPU or TCU processes never call
numerical backend evolution. The adapters are:

| Backend | Implemented capability | Limits |
| --- | --- | --- |
| Scripted | Deterministic token-indexed measurement bits and protocol testing. | Does not model a quantum state. |
| Aer | Persistent statevector, supported one- and two-qubit gates, joint mid-circuit measurement and collapse. | 1–20 qubits; no pulse integration or noise model in v1. |
| Pulse | Aer gates and measurement plus joint piecewise-constant X, Y and Z Hamiltonian evolution using SciPy `expm`. | 1–8 qubits; ideal closed-system constant drives, no waveform samples or dissipative solver. |

Pulse amplitude is angular frequency in radians per nanosecond, with Hamiltonian
`H = sum(amplitude * Pauli / 2)` and hbar set to one. Rotation-gate amplitude is its
angle in radians. Qubit zero is the least significant statevector bit. Host execution
time never changes timestamps. A backend failure terminates the run; it is not retried
against a partly changed quantum state.

## Scope

All 21 logical modules have an executable implementation or, for future synchronization,
an explicit rejecting boundary. RV32I privileged mode, compressed instructions, interrupts,
caches, distributed synchronization, TQEC input, arbitrary sampled waveforms, dissipation,
and GPU adapters are future work. ECALL and EBREAK produce typed traps; FENCE.I and
unselected instruction extensions reject. This version is an architecture simulator,
not an RTL model, hardware certification, or a general-purpose RISC-V operating-system platform.
