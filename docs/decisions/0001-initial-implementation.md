# ADR 0001: Initial executable profile

Date: 2026-09-16. Status: accepted for implementation; verification is in progress.

## Scope and ownership

Implement all Phase 1 module contracts. The future synchronization adapter rejects
requests with `UnsupportedSynchronization`; distributed pause/resume remains Phase 2.
The baseline protocol in `module-architecture.md` is unchanged. CACTUS comparison is
an external acceptance activity, never a source or build dependency of the simulator.

## CPU and memory

The initial replaceable CPU uses an in-order three-stage fetch, decode and execute/commit
pipeline. Each latch advances at most once per CPU edge. An instruction commits only
from the oldest execute stage. Operands are read when entering execute, with explicit
same-edge forwarding of the preceding commit. Loads hold execute until their response;
younger decode and fetch instructions remain held. Taken branches flush both younger
stages. A blocked extension holds its identity and operands until its completion rule
is satisfied. Faults suppress younger effects.

Instruction and data requests use independent bounded ports of one memory owner, which
supports one outstanding request per port. Both requests and responses obey strict-edge
visibility. Memory completion latency is counted from its acceptance edge. Fetches are
speculative; their faults are delivered only when that instruction becomes oldest.
All instructions are 32-bit. RV32I ECALL and EBREAK terminate with distinct typed traps;
privileged instructions and unselected ISA extensions are illegal instructions. FENCE
orders this in-order blocking memory model. FENCE.I is not part of the selected ISA.

## Quantum extension v1

Use the RISC-V custom-0 opcode `0x0b` and the R-type operand fields. The simulator loads
machine code; GNU assembler `.insn` macros are the independent encoding toolchain.

| funct3 | Name | Operands and completion |
| --- | --- | --- |
| 0 | QAPPEND | rs1 is port; rs2 is codeword; rd receives a nonzero measurement handle for acquisition, otherwise zero. Complete on ProducerAccepted. |
| 1 | QADVANCE | rs1 is unsigned interval; rd and rs2 must be zero. Complete after required group admission. |
| 2 | QFLUSH | All register fields must be zero. Complete after required admission. |
| 3 | QREAD | rs1 is a live measurement handle, rs2 zero; rd receives the bit. Flush, then wait for CPU-visible result and consume it. |
| 4 | QEND | All register fields zero. Close production after flush; successful simulator stop still requires drain. |
| 5 | QAPPEND_IF | rs1 is port, rs2 codeword, rd names a register holding an earlier measurement handle; funct7 selects expected bit 0 or 1. No register is written. |
| 6 | QSYNC | Reserved synchronization boundary; always faults in this profile. |
| 7 | reserved | Illegal instruction. |

funct7 must be zero except QAPPEND_IF. Encodings with other fixed fields are rejected.
The 32-bit architectural measurement handle refers to a checked CPU-owned record with
epoch, measurement ID, slot and generation; no generation bits are silently truncated.
Fast conditions snapshot the stable measurement identity at APPEND, independent of
whether its CPU slot is subsequently consumed. Conditional acquisition is rejected in
v1, avoiding a pending token for an operation that never executes.

## Default numerical profile

Time resolution is 1 ns. CPU and memory period is 5 ticks; TCU period is 20 ticks;
initial phases are zero and logical TCU cycle zero occurs at tick 1000. Crossings use
one receiver edge, except the independently configurable fast-result path defaults to
two. Memory completion latency is one memory edge after acceptance. Capacities default
to 32 timing points, 32 events per port, 16 staged actions and 8 measurement slots.
One group can be admitted and at most one old timing point can fire on a TCU edge;
each port fires at most one action per group. Queue credits use old occupancy.

The immutable profile also contains the port action map, positive action durations,
output delays, discriminator arm delays, discriminator processing delays, qubit count,
seed and watchdog. CLI overrides create a new validated profile before elaboration.
Every run records the complete profile and a stable fingerprint. External comparison
selects its own explicit numerical configuration through this generic interface.

## Backend and scheduling

Use SystemC 3.0.1 compiled as C++20. The CPU, memory and TCU own pure transition objects.
Committed tick-stamped mailboxes enforce visibility independently of runnable order.
Device boundary collection has an explicit per-tick barrier after the TCU transition,
including zero-delay launches. The quantum backend never advances SystemC time.

Provide a scripted backend for deterministic protocol tests, a live Python adapter to
Qiskit Aer 0.17.2 with Qiskit 2.1.2, and a small-system piecewise-constant Hamiltonian
backend using SciPy 1.16.2. The latter jointly integrates active drives, rather than
claiming pulse support from ideal-gate replay. Python calls are synchronous host work;
all physical times and result publication remain controlled by DeviceRuntime.

Numerical backends maintain one shared state across operations and mid-circuit
measurements. Backend capabilities are checked before whole-batch mutation. Changing
the selected backend does not alter CPU or TCU timing.

## Validation boundaries

TCU differential validation compares corresponding group-output events on the common
positive-interval domain. Complete workload validation additionally compares actual
device operations and supported measurement feedback. Different internal structures
do not require identical private queue occupancy or speculative CPU activity.
Unavailable reference features are explicit coverage limitations, never passing tests.
Public trace events provide operation identity, epoch, physical tick, local cycle,
operation, targets, status and relevant queue counts. No per-event time alignment is
permitted. The first release must carry evidence for its supported comparisons.

## CPU construction boundary

`Simulator` accepts an optional CPU factory taking a plain `Clock`, entry PC and
`Trace` reference and returning `ICpuCycleModel`. The default factory constructs the
RV32I three-stage model. This makes replacement executable through composition; an
external adapter does not require editing the SystemC scheduling owner. The factory
must return a valid model and use the same reset and oldest-publication contract.
