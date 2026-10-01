# ADR 0001: Initial CPU and control profile

Date: 2026-09-16. Status: accepted.

## Context

The simulator needs an executable RV32I front end and a timed quantum control
path. The control path follows the QuMA approach: the CPU prepares operations
ahead of time, and a timing control unit releases them at their planned cycles.
The CPU and quantum backend must remain replaceable without changing that protocol.

## Decision

Use C++20 and SystemC. Keep instruction semantics and state transitions in
ordinary C++ objects. `Simulator` owns the SystemC processes and calls those
objects at CPU edges, TCU edges and scheduled device boundaries.

The initial CPU has three in-order stages: fetch, decode and execute.
The execute stage also commits results.
Each stage advances at most once per CPU edge. Operands are read on entry to
execute, with forwarding from the instruction that commits on that edge. A
load holds execute until its response arrives. A taken branch flushes younger
instructions, and a blocked quantum instruction retains its identity and operands.

Instruction and data accesses use independent bounded memory ports. Each port
allows one outstanding transaction. Requests and responses become visible only
on a receiver edge strictly after publication; memory completion latency starts
at acceptance. Speculative fetch faults are deferred until the fetched
instruction becomes the oldest instruction.

### Program and instruction boundary

Load 32-bit RV32I machine code from an ELF file or raw binary. Keep assembly
outside the simulator. The examples use GNU assembler macros to encode the
quantum extension in the RISC-V custom-0 opcode space.

QAPPEND stores events for the current time point before they enter the TCU.
Several instructions can therefore contribute events
to one planned cycle. QADVANCE and QFLUSH wait for any required enqueue reply.
QREAD flushes before waiting for a measurement; QEND flushes before closing
production. The [instruction reference](../interfaces.md#quantum-instruction-encoding)
defines the encodings and completion rules.

Measurement handles refer to records with an epoch, measurement ID, slot and
generation. Fast conditions retain the exact measurement identity even after the
CPU consumes its result slot. Conditional acquisition is unsupported because a
cancelled acquisition would leave its result handle unresolved. QSYNC returns
`UnsupportedSynchronization`.

### Timing and backend boundary

A validated profile fixes clocks, crossing delays, capacities and event maps
before simulation starts. The [implementation reference](../implementation.md#default-timing)
lists the defaults. Mailboxes retain payloads and arrival ticks;
SystemC events wake processes without carrying the payload themselves.

At each device event tick, the barrier waits for all due clocked methods
to finish before processing device events. This includes events triggered with zero output delay on
that tick. `ControlElectronics` controls evolution intervals, measurements and
result publication. A backend computes quantum state changes synchronously
and never advances SystemC time.

The built-in mock backend supports deterministic protocol tests. Optional
Python adapters provide Qiskit Aer simulation and a small-system
piecewise-constant Hamiltonian backend. Each numerical backend maintains one
shared state across operations and mid-circuit measurements. Capability checks
validate all events at a tick before quantum state changes.

### Replacement interfaces

`Simulator` accepts a CPU factory that takes a `Clock`, entry PC and `Trace`
reference and returns an `ICpuCycleModel`. An external CPU adapter must obey the
same edge, reset and instruction-publication contracts. The default factory
creates the three-stage RV32I model.

Quantum backends implement `IQuantumBackend` and reject unsupported events in `validate()`.
Replacing a backend changes state evolution and measurement results while the
controller retains ownership of operation timing.

## Consequences

The default CPU provides a testable three-stage model. Its cycle counts depend
on that pipeline and the configured memory delays. Other CPU implementations
can use different pipelines while preserving the control interfaces.

The simulator does not execute eQASM binaries. Distributed synchronization and
conditional acquisition are unsupported.
