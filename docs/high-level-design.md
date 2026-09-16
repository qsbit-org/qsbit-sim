# High-Level Design

**Status:** Phase 1 requirements agreed; detailed ISA encoding and timing profiles remain design proposals, 2026-09-16.

## 1. Goal and phase boundaries

qsbit-sim is an event-driven, cycle-level architecture simulator for a programmable quantum control processor. It is implemented in C++20 and SystemC, not as an RTL simulator. The processor executes RV32I machine code plus versioned quantum-control extensions inspired by Distributed-HISQ. A QuMA-inspired timing control unit (TCU) decouples command issue from precisely timed device output. The architecture must permit replacement of the instruction extensions, CPU microarchitecture, and quantum device backend without changing the other two domains.

**Phase 1 acceptance:** Compile the same control workload to a CACTUS eQASM program and a qsbit-sim RV32I-extension program. Run each program independently with matched clocks, queue and device configuration, and deterministic measurement input. At the quantum-control boundary, their normalized command acceptance, timed output, measurement availability, and conditional-feedback traces must have the same event count, order, operation, target, and absolute simulation time. The tolerance is zero common-grid ticks. Different binary encodings, instruction counts, PCs, and internal CPU traces are allowed. qsbit-sim may not consume a CACTUS output trace as an execution schedule. The comparison applies to workloads whose control operations are expressible in both systems; unsupported operations fail explicitly rather than being dropped. A scripted device isolates control timing from numerical state-simulator differences. See the differential protocol in Section 8.

A smaller **M0 smoke-test milestone** may use RV32I, memory-mapped control registers, one queue, and a scripted device to prove the event path. M0 is not Phase 1 acceptance. Phase 1 adds RV32I extension instructions, the concurrency required by selected CACTUS workloads, a defined CPU timing profile, and two device-adapter prototypes: one circuit-level and one small-system pulse-level path. Phase 2 studies an adapter from TQEC workloads to the Phase 1 program and trace contracts. TQEC does not determine Phase 1 instruction, time, or backend semantics.

## 2. Design principles and ownership

1. **Separate ISA, microarchitecture, and control timing.** The ISA library decodes a 32-bit word and specifies architectural effects. A CPU microarchitecture owns pipeline occupancy, forwarding, hazards, requests, retirement, and the actual PC and register state. The TCU owns queueing and timed dispatch. The ISA library does not update a second copy of PC or registers.
2. **Keep SystemC at the scheduling boundary.** Plain C++20 value types describe decoded operations, cycle inputs and outputs, commands, feedback, and traces. SystemC modules provide clocks, event scheduling, and ports. No SystemC type appears in the public CPU adapter contract.
3. **Make backpressure and concurrency explicit.** Commands have stable identities; a blocked command is accepted exactly once. Resources, per-port queues, output lanes, feedback buffers, and cross-domain synchronizers have configured capacities and delays. Simultaneous operations on independent ports are representable.
4. **Preserve deterministic time.** The scheduler owns simulation time. Each clock domain has a period and phase; same-time sampling, acceptance, state commit, and output visibility have a specified order. Host execution time never advances simulated hardware time.
5. **Keep the quantum backend replaceable.** The backend owns quantum state or dynamics. It reports capabilities and returns measurement values. Control, readout, and decoder latencies remain in the architecture model, not in the numerical backend.

## 3. Components and data flow

```text
ELF image -> RV32I and extension decoder -> CPU timing model -> control command adapter
                       |                         |                    |
                  ISA semantics              memory bus         command acceptance
                                                                     |
                                                        quantum pipeline and TCU
                                                                     |
                                                        channel and readout model
                                                                     |
                                                      quantum backend adapter
                                                                     |
                                              measurement and feedback to CPU
```

The ELF loader maps executable segments and entry PC into simulated memory. The ISA library returns a `DecodedOperation` with operand fields, architectural effect, and declared resource usage. The CPU timing model owns register and PC state, pipeline progression, memory and command-port stalls, exception delivery, and retirement. The command adapter converts a committed extension instruction or an M0 MMIO write into the same `TimedCommand` protocol. The TCU and quantum pipeline map codewords to device actions and schedule them on independent clocks. The device model handles channel occupancy, readout, and feedback visibility. A device backend performs quantum evolution or returns scripted measurements.

The first CPU model may be a single-issue in-order pipeline, but its stage count and latency profile are explicit configuration, not part of RV32I semantics. Future CPU backends may run in a functional mode that does not claim CACTUS timing equivalence. A backend may claim the Phase 1 timing profile only after passing its cycle and boundary-trace tests. Architectural retirement equivalence alone is insufficient for that claim.

## 4. ISA and toolchain boundary

The simulator reads machine code, not assembly text. Its loader accepts RV32 ELF and, for focused tests, a raw binary with an explicit load address. Fetch reads a 32-bit word; a versioned decoder recognizes RV32I and enabled custom encodings, otherwise it raises an illegal-instruction result. The loader checks ELF class, machine, endianness, loadable segments, and entry point. A project-specific extension manifest may permit an early compatibility check; ordinary ELF need not contain that manifest, so runtime illegal-encoding checks remain mandatory.

The assembler and linker are a separate toolchain concern. Phase 1 uses an existing GNU or LLVM RISC-V assembler with `.insn` and thin macros or inline assembly for custom instructions. There is no need for a flex and bison parser in qsbit-sim. A separate toolchain project can own pinned tools, macros, startup code, linker script, disassembly support, and eventually native mnemonics or compiler lowering. Both projects must consume or verify one versioned ISA specification containing encoding masks, operands, register writes, exceptions, blocking behavior, and architectural effects. Tests assemble each extension, load its ELF, decode it in qsbit-sim, and check execution and disassembly. Fix toolchain versions and disable unintended compressed instructions and link-time relaxation in timing fixtures.

The initial extension profile uses port and codeword commands, waiting, and measurement-result access; message and synchronization instructions are reserved for later profiles. Instruction encodings are not asserted to be binary-compatible with Distributed-HISQ until checked against an authoritative encoding specification. An M0 MMIO doorbell is only another producer of `TimedCommand`, not the Phase 1 software-facing instruction set.

## 5. CPU and control-port contract

All public records use fixed-width values and a versioned serialized form. They include:

- `CpuCycleInput`: CPU edge time and cycle, memory response, command ready or reject status, visible feedback, and reset state.
- `CpuCycleOutput`: optional memory request, a held command proposal with identity, retirement records, exception, stall reason, and next-edge scheduling need.
- `TimedCommand`: command ID, originating instruction ID, operation kind, target port and resource, codeword or payload ID, requested TCU time, and correlation ID.
- `Feedback`: measurement ID, value, status, device completion time, and CPU-visible time.
- `TraceEvent`: event kind, global time, clock domain and cycle, identity, port, operation, and typed status.

A command proposal remains stable while ready is false. Acceptance occurs on one defined receiver edge and returns an acknowledgment; that acknowledgment authorizes the instruction's architectural completion at the model's stated stage. No command may be accepted twice when backpressure clears. Memory requests use the same request, response, and completion discipline. The SystemC adapter translates these records to clocked events but cannot silently alter their acceptance semantics.

A future external RISC-V simulator may connect through a C-compatible adapter or an out-of-process protocol rather than a C++ ABI. The adapter must provide a way to block instruction completion or memory transactions until control-port acceptance, expose the time and order of retirement and side effects, and consume feedback at a defined edge. If it cannot, it is a functional backend only. A timing-compatible backend must additionally demonstrate the same boundary-event timing profile. This permits mature simulators to be integrated without promising that every CPU automatically reproduces CACTUS cycles.

## 6. Clock, TCU, and device semantics

The scheduler uses an integer global time unit selected before elaboration. CPU and TCU periods and phases are separate. An event carries its global time and clock-domain cycle; conversions require exact representability or an explicit, tested rounding rule. At a coincident edge, all modules read the prior committed state, compute next state, then commit once. Queue acceptance, dispatch, and newly visible feedback have a fixed protocol order. SystemC delta-cycle ordering must not accidentally become a hardware timing rule.

The TCU accepts commands into bounded queues, checks target time, and dispatches at the configured TCU edge when the addressed lane is available. It records requested time and actual dispatch time separately. A command already late at acceptance or blocked past its deadline produces a specified error event; the model never silently calls a late output on time. Equal-time ordering is stable and documented. Queue depth, almost-full threshold, number of ports and lanes, codeword map, cross-domain delay, readout time, and error policy are configuration values.

M0 can use one queue and lane. Phase 1 must support the simultaneous target ports and resource conflicts exercised by its CACTUS comparison suite. This need not copy CACTUS internals: the comparison profile records the selected CACTUS revision, entry point, actual clock periods, reset release, queue and device configuration, and trace probes. In the reviewed CACTUS source, the testbench entry's nominal `clock_200MHz` is configured with a 2 ns period, while the server entry uses 5 ns; both use a 20 ns second clock. Test fixtures must name the entry and use actual configured periods rather than the clock variable name.

## 7. Quantum backend contract

The architecture owns simulated time and hardware latency; the numerical backend owns state evolution. A baseline adapter can reset, advance to a time, apply a gate, measure with collapse, and declare capabilities. A pulse adapter additionally evolves a time interval under all overlapping drives and a configured Hamiltonian; it cannot emulate overlapping pulses by blindly replaying independent gates. Required capabilities include gate stepping, mid-circuit measurement with collapse, pulse evolution, noise support, and state save or restore. A workload whose required capability is absent fails clearly.

The scripted backend supplies fixed measurement values for CACTUS timing tests. During Phase 1, implement one circuit-level adapter prototype and one small-system pulse-level adapter prototype to test the contract; their selected libraries and supported operation subsets must be documented before coding. Qiskit Aer and cuStateVec are candidate circuit-level paths, with different APIs and resource requirements. QuTiP is a candidate pulse-level path. Qiskit Dynamics remains an optional adapter target because its official repository is archived and no longer actively maintained. Python adapters may be bridged with pybind11 at gate, pulse-segment, or measurement boundaries, never at every CPU cycle. Backend computation time is a host performance metric; measurement visibility time comes from the readout model.

## 8. Differential verification against CACTUS

An ISA-neutral workload description identifies operations, ports, waits, measurements, dependencies, and conditional actions. It is compiled separately to CACTUS eQASM and qsbit-sim RV32I-extension binaries. Each executable runs independently against equivalent codeword tables and scripted measurement streams. CACTUS and qsbit-sim emit read-only boundary probes with a shared trace schema. Normalize only coding names and the common post-reset time origin; do not shift individual events to make them match.

For every case, compare event count, order, operation, port, measurement correlation, and absolute global time with zero common-grid-tick tolerance. Compare command acceptance, TCU dispatch, device pulse start and end, measurement result visibility, and feedback command output wherever both models expose them. A missing probe is an incomplete test, not a pass. On a mismatch, report the first divergent event plus CPU stall reason, queue occupancy, and the last measurement transition on each side. Tests cover a single command, explicit wait, two simultaneous ports, near-full queue and backpressure, both feedback branches, repeated measurements, and a longer CACTUS example. Unsupported semantic mappings are reported as unsupported; they are never silently omitted. Deterministic unit schedules complement this differential gate but do not replace it.

## 9. Milestones and unresolved choices

1. **M0:** RV32I ELF execution, an explicit CPU timing model, SystemC event path, MMIO command smoke test, scripted backend, structured trace, and unit plus integration tests.
2. **Phase 1 core:** versioned RV32I extension profile, assembler macros, command backpressure, concurrent output resources, readout and feedback, and the complete CACTUS differential gate.
3. **Phase 1 backend validation:** a circuit-level live measurement adapter and a small-system pulse-level adapter through the same capability contract.
4. **Phase 2:** TQEC workload generation and result mapping through existing operation IDs, measurement IDs, configuration, and trace interfaces.

Before implementation, record decisions for the first CPU stage and memory-latency profile, custom instruction encodings, CACTUS fixture and probe points, TCU queue and lane configuration, same-time event order, and the two backend adapter prototypes. These parameters are proposals until measured or fixed in a dated architecture decision record; the Phase 1 acceptance criterion above is not optional.

## Primary references

- [CACTUS source](https://github.com/gtaifu/CACTUS), for the architecture comparison baseline.
- [Distributed-HISQ paper](https://arxiv.org/abs/2509.04798), for RV32I extension, port and codeword, TCU, and synchronization concepts.
- [QuMA paper](https://arxiv.org/abs/1708.07677), for codeword-based and queue-based timing control.
- [RISC-V Unprivileged ISA specification](https://docs.riscv.org/reference/isa/unpriv/unpriv-index.html), for RV32I behavior and custom encoding space.
- [RISC-V assembly manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc) and [GNU `.insn` formats](https://sourceware.org/binutils/docs/as/RISC_002dV_002dFormats.html), for extension assembly without a new parser.
- [RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/), for program image conventions.
- [Accellera SystemC reference implementation](https://github.com/accellera-official/systemc), for simulation semantics.
- [Qiskit Aer](https://github.com/Qiskit/qiskit-aer), [NVIDIA cuStateVec](https://docs.nvidia.com/cuda/cuquantum/latest/custatevec/index.html), [QuTiP](https://qutip.readthedocs.io/en/latest/), and [Qiskit Dynamics archive](https://github.com/Qiskit-Community/qiskit-dynamics), for backend capability evaluation.
