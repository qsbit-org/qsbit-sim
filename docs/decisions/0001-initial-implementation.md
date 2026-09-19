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

The initial CPU has three in-order stages: fetch, decode and execute/commit.
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

The extension separates building a group from admitting it to the TCU. APPEND
completes after local staging, so several instructions can contribute actions
to one planned cycle. ADVANCE and FLUSH wait for any required admission reply.
READ_RESULT flushes before waiting for a measurement; END flushes before closing
production. The [instruction reference](../interfaces.md#quantum-instruction-encoding)
defines the encodings and completion rules.

Measurement handles refer to records with an epoch, measurement ID, slot and
generation. Fast conditions retain the exact measurement identity even after the
CPU consumes its result slot. Conditional acquisition is unsupported because a
cancelled acquisition would leave its result handle unresolved. QSYNC returns
`UnsupportedSynchronization`.

### Timing and backend boundary

A validated profile fixes clocks, crossing delays, capacities and action maps
before simulation starts. The [implementation reference](../implementation.md#default-timing)
lists the defaults. Committed mailboxes carry payloads and eligibility ticks;
SystemC events wake processes without carrying the payload themselves.

At each physical boundary, the device barrier checks completion of all due clock transitions before
processing device actions. This includes actions launched with zero delay on
that tick. `DeviceRuntime` controls evolution intervals, measurements and
result publication. A backend computes quantum state changes synchronously
and never advances SystemC time.

The built-in scripted backend supports deterministic protocol tests. Optional
Python adapters provide Qiskit Aer simulation and a small-system
piecewise-constant Hamiltonian backend. Each numerical backend maintains one
shared state across operations and mid-circuit measurements. Capability checks
run before a complete boundary batch changes state.

### Replacement interfaces

`Simulator` accepts a CPU factory that takes a `Clock`, entry PC and `Trace`
reference and returns an `ICpuCycleModel`. An external CPU adapter must obey the
same edge, reset and instruction-publication contracts. The default factory
creates the three-stage RV32I model.

Quantum backends implement `IQuantumBackend` and reject unsupported actions in `validate()`.
Replacing a backend changes state evolution and measurement results while the
controller retains ownership of operation timing.

## Consequences

The initial CPU is small enough to test directly, but its cycle counts do not
claim compatibility with an existing processor. Classical pipelines may differ
in an external comparison as long as both produce the required timed quantum
operations.

The simulator does not execute eQASM binaries. Distributed synchronization and
conditional acquisition require additional contracts before implementation.
