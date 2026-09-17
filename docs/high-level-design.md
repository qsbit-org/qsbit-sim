# High-Level Design

**Status:** executable v0.1.0 baseline, 2026-09-17. [ADR 0001](decisions/0001-initial-implementation.md) fixes the initial profile; [implementation.md](implementation.md) documents code ownership and supported capabilities.

The [glossary](glossary.md) defines the timing and control terms used below.

## 1. Goal and phase boundaries

qsbit-sim is an event-driven, cycle-level architecture simulator for a programmable quantum control processor. It is implemented in C++20 and SystemC, not as an RTL simulator. The processor executes RV32I machine code plus versioned quantum-control extensions inspired by Distributed-HISQ. A QuMA-style timing control unit (TCU) uses a timing queue, label-tagged event queues, and a deterministic-domain timer to decouple command preparation from precisely timed device output. The architecture must permit replacement of the instruction extensions, CPU microarchitecture, and quantum device backend without changing the other two domains.

**Phase 1 acceptance:** Compile the same control workload to a CACTUS eQASM program and a qsbit-sim RV32I-extension program. Run each program independently with matched clocks, queue and device configuration, and deterministic measurement input. At the quantum-control boundary, their normalized timed output, measurement availability, and supported conditional-feedback traces must have the same event count, order, operation, target, and absolute simulation time. The tolerance is zero common-grid ticks. Different binary encodings, instruction counts, PCs, and internal CPU traces are allowed. qsbit-sim may not consume a CACTUS output trace as an execution schedule. The comparison applies to workloads whose control operations are expressible in both systems; unsupported operations fail explicitly rather than being dropped. A scripted device isolates control timing from numerical state-simulator differences. See the differential protocol and code boundary in Section 8.

The implemented baseline uses RV32I custom instructions, bounded timing and per-port event queues, a scripted backend, and optional circuit and pulse adapters. Phase 2 will add TQEC workload and result adapters through the existing program and trace boundaries.

## 2. Design principles and ownership

1. **Separate ISA, microarchitecture, and control timing.** The ISA library decodes a 32-bit word and specifies architectural effects. A CPU microarchitecture owns pipeline occupancy, forwarding, hazards, requests, retirement, and the actual PC and register state. The TCU owns queueing and label firing. The ISA library does not update a second copy of PC or registers.
2. **Keep SystemC at the scheduling boundary.** Plain C++20 value types describe decoded operations, cycle inputs and outputs, commands, feedback, and traces. SystemC modules provide clocks, event scheduling, and ports. No SystemC type appears in the public CPU adapter contract.
3. **Make backpressure and concurrency explicit.** Commands have stable identities; a blocked command is accepted exactly once. Resources, per-port queues, output lanes, feedback buffers, and cross-domain mailboxes have configured capacities and delays. Simultaneous operations on independent ports are representable.
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

The ELF loader maps executable segments and entry PC into simulated memory. The ISA library returns `rv32::Decoded` operand fields and `rv32::Effect` architectural effects. The CPU timing model owns register and PC state, pipeline progression, memory and command-port stalls, exception delivery, and retirement. The command adapter accepts only an authorized, non-speculative extension instruction. The CPU-side producer first collects operations planned for one logical TCU cycle in a bounded open group. Successive APPEND instructions can finish after entering that group. A later sealing operation fixes its contents and submits the whole group for atomic TCU admission. The CPU retirement point is defined for each operation. The quantum pipeline combines same-time events and resolves their device actions before admission to label-tagged per-port queues; the TCU timer broadcasts each reached label to trigger a complete batch on its own clock. The device model handles channel occupancy, readout, and feedback visibility. A device backend performs quantum evolution or returns scripted measurements.

The first CPU model is a single-issue, three-stage in-order pipeline. Its memory latency is configurable; changing stage topology requires another ICpuCycleModel implementation, not an ISA change. Future CPU backends may run in a functional mode that does not claim CACTUS timing equivalence. A backend may claim the Phase 1 timing profile only after passing its cycle and boundary-trace tests. Architectural retirement equivalence alone is insufficient for that claim.

## 4. ISA and toolchain boundary

The simulator reads machine code, not assembly text. Its loader accepts RV32 ELF and, for focused tests, a raw binary with an explicit load address. Fetch reads a 32-bit word; a versioned decoder recognizes RV32I and enabled custom encodings, otherwise it raises an illegal-instruction result. The loader checks ELF class, machine, endianness, loadable segments, and entry point. A project-specific extension manifest may permit an early compatibility check; ordinary ELF need not contain that manifest, so runtime illegal-encoding checks remain mandatory.

The assembler and linker are a separate toolchain concern. Phase 1 uses an existing GNU or LLVM RISC-V assembler with `.insn` and thin macros or inline assembly for custom instructions. There is no need for a flex and bison parser in qsbit-sim. A separate toolchain project can own pinned tools, macros, startup code, linker script, disassembly support, and eventually native mnemonics or compiler lowering. Both projects must consume or verify one versioned ISA specification containing encoding masks, operands, register writes, exceptions, blocking behavior, and architectural effects. Tests assemble each extension, load its ELF, decode it in qsbit-sim, and check execution and disassembly. Fix toolchain versions and disable unintended compressed instructions and link-time relaxation in timing fixtures.

The initial extension profile uses port and codeword commands, waiting, and measurement-result access; message and synchronization instructions are reserved for later profiles. Instruction encodings are not asserted to be binary-compatible with Distributed-HISQ until checked against an authoritative encoding specification.

## 5. CPU and control-port contract

The C++ boundaries use fixed-width architectural values. JSON configuration and trace formats are versioned separately. The [interface declarations](cpp-interfaces.md) define the available fields:

- `ICpuCycleModel::step(Tick, Epoch, CpuPorts&)`: one CPU-edge transition. `CpuPorts` supplies bounded fetch and data request/response channels and a producer-operation callback; the CPU owns held operations and retirement.
- `ProducerOperation`: stable instruction ID, semantic operation, captured operands and optional predicate handle; completion returns an optional 32-bit result. A missing result means the same operation must remain held.
- `TimingPoint` and `ReservedEvent`: epoch, timing interval and label, exact member manifest, event and instruction IDs, port, codeword, resolved action descriptor and optional measurement token. A derived global fire tick is valid only while the future TCU run state is known; it does not replace the timing queue.
- `Completion`: full measurement token and value; committed mailbox envelopes carry epoch and receiver-visible tick. Sampling, readiness and visibility are separate trace events.
- `TraceEvent`: event kind, global tick, epoch, identity, local cycle, port, operation and kind-specific payload; fault records contain a typed error name.

Producer acceptance and TCU group admission are separate acknowledgments: `ProducerAccepted` means an APPEND occupies bounded CPU-side staging, while `GroupAdmitted` means a sealed group has entered all required TCU queues. APPEND retires on the former; positive ADVANCE and FLUSH wait for the latter when they seal a real group. READ_RESULT first flushes, then waits for its token; END seals and initiates drain. The selected v1 encodings are specified in ADR 0001. Waiting for TCU admission on every scalar APPEND would prevent later instructions from completing its group. Oversized groups fault immediately. Stable epoch-tagged IDs prevent duplicate acceptance, and only the oldest non-speculative instruction may publish an irreversible side effect. See the [timing terms](module-architecture.md#timing-terms-used-below) and [shared protocol](module-architecture.md#4-baseline-protocol-and-event-ordering) for exact completion and crossing rules.

A future external RISC-V simulator may connect through a C-compatible adapter or an out-of-process protocol rather than a C++ ABI. The adapter must provide a way to block each instruction or memory transaction until its specified producer acceptance, group admission, or result completion, expose the time and order of retirement and side effects, and consume feedback at a defined edge. If it cannot, it is a functional backend only. A timing-compatible backend must additionally demonstrate the same boundary-event timing profile. This permits mature simulators to be integrated without promising that every CPU automatically reproduces CACTUS cycles.

## 6. Clock, TCU, and device semantics

The scheduler uses an integer global time unit selected before elaboration. CPU and TCU periods and phases are separate, and the baseline rejects unrepresentable times. Cross-owner mailboxes use prior committed data: publication at tick p becomes eligible on the first receiver edge strictly after p, plus N-1 further receiver periods for a configured latency N >= 1. TCU admission uses old occupancy, cannot spend same-edge freed capacity, and cannot fire a newly admitted group on that edge. Logical substages within one clock-domain owner add no hidden cycles. DeviceRuntime explicitly batches physical events after that tick's edge transitions; incidental SystemC process order cannot select hardware behavior.

Multiple APPEND operations can enter the same open group. ADVANCE(0) neither seals that group nor moves the cursor. A sealing operation fixes its interval, label and member manifest. TCU admission appends each point and all its port events atomically. With start tick S and period P, deterministic cycle n occurs at S+nP, with cycle zero on the start edge. The timer broadcasts the reached label; launch preflight checks the entire group and physical resource intervals before outputs. Empty queues are observable streaming gaps, not automatic errors: the timer continues and future groups retain their original cumulative due cycles. Admission at or after a due edge faults, as do manifested missing members and resource conflicts. END closes production and successful stop waits for queue, physical-action and measurement drain. Session reset invalidates old-epoch callbacks without rewinding global time. See [Module Architecture and QuMA-Style Timing Control](module-architecture.md) for complete rules.

The baseline supports the simultaneous target ports and resource conflicts exercised by its CACTUS comparison suite. This need not copy CACTUS internals: the comparison profile records the selected CACTUS revision, entry point, actual clock periods, reset release, queue and device configuration, and trace probes. In the reviewed CACTUS source, the testbench entry's nominal `clock_200MHz` is configured with a 2 ns period, while the server entry uses 5 ns; both use a 20 ns second clock. Test fixtures must name the entry and use actual configured periods rather than the clock variable name.

## 7. Quantum backend contract

The architecture owns simulated time and hardware latency; one quantum-state service owns numerical backend access. It evolves the prior active drive set once to each physical boundary, then processes a complete same-time action batch. Individual output ports cannot advance shared state independently. A baseline adapter can reset, advance to a time, apply a gate, measure with collapse, and declare capabilities. A pulse adapter additionally evolves a time interval under all overlapping drives and a configured Hamiltonian; it cannot emulate overlapping pulses by blindly replaying independent gates. The capability vocabulary includes gate stepping, mid-circuit measurement with collapse, pulse evolution, noise support, and state save or restore; each workload selects its required subset. A circuit adapter is not required to implement pulse evolution or every optional capability. A workload whose required capability is absent fails clearly.

The scripted backend supplies fixed measurement values for protocol tests. The implemented circuit adapter uses Qiskit Aer; the small-system pulse adapter uses SciPy matrix exponentiation with Aer measurement. Their supported subsets and units are documented in [implementation.md](implementation.md). cuStateVec remains a future circuit-level adapter with different API and resource requirements. QuTiP is a candidate pulse-level path. Qiskit Dynamics remains an optional adapter target because its official repository is archived and no longer actively maintained. Python adapters may be bridged with pybind11 at gate, pulse-segment, or measurement boundaries, never at every CPU cycle. Backend computation time is a host performance metric; measurement visibility time comes from the readout model.

## 8. Differential verification against CACTUS

The entire CACTUS comparison implementation belongs to a separate, disposable validation project outside this repository. That project owns the CACTUS checkout and any probes or patches, eQASM translation, paired workloads, fixtures, runners, normalization, comparison logic, configurations, and result artifacts. It invokes qsbit-sim through its public CLI or versioned trace interface. The simulator core provides a generic, versioned `TraceEvent` stream because it is useful for debugging and other consumers; it contains no CACTUS-specific types, flags, imports, build targets, or runtime branches. The validation project is not a default build or CI dependency. Deleting it must require no change to qsbit-sim source, tests, or build files. Phase 1 timing acceptance is performed by running this external project against a pinned simulator revision.

An ISA-neutral workload description identifies operations, ports, waits, measurements, dependencies, and conditional actions. It is compiled separately to CACTUS eQASM and qsbit-sim RV32I-extension binaries. Each executable runs independently against equivalent codeword tables and scripted measurement streams. The external project extracts CACTUS boundary events through its own probes and reads qsbit-sim's generic trace stream. It maps both into a comparison schema. Normalize only coding names and the common post-reset time origin; do not shift individual events to make them match.

For every case, compare event count, order, operation, port, measurement correlation, and absolute global time with zero common-grid-tick tolerance. Compare label firing, physical operation starts, result readiness, CPU-visible results and feedback output wherever both models implement the corresponding boundary. Producer acceptance and private admission times are diagnostic because CPU pipelines differ. Native tests verify physical ends when the reference exposes no end probe. The fixture names the counterpart of each milestone; producer acceptance cannot substitute for group admission. A missing probe is an incomplete test, not a pass. On a mismatch, report the first divergent event plus CPU stall reason, queue occupancy, and the last measurement transition on each side. Tests cover a single command, explicit wait, two simultaneous ports, near-full queue and backpressure, both feedback branches, repeated measurements, and a longer CACTUS example. Unsupported semantic mappings are reported as unsupported; they are never silently omitted. Deterministic unit schedules complement this differential gate but do not replace it.

## 9. Scope and extensions

The implemented baseline includes RV32I ELF execution, a three-stage CPU model, custom quantum instructions, QuMA-style timing and event queues, readout and feedback, and scripted, Aer and small-system pulse backends. TQEC integration and distributed synchronization remain future extensions.

[ADR 0001](decisions/0001-initial-implementation.md) records the selected profile. [ADR 0002](decisions/0002-reference-comparison-scope.md) records the reference comparison limits. Those limits apply to every timing-equivalence claim; new profiles require their own regression evidence.

## Primary references

- [CACTUS source](https://github.com/gtaifu/CACTUS), for the architecture comparison baseline.
- [Distributed-HISQ paper](https://arxiv.org/abs/2509.04798), for RV32I extension, port and codeword, TCU, and synchronization concepts.
- [QuMA paper](https://arxiv.org/abs/1708.07677), for codeword-based and queue-based timing control.
- [RISC-V Unprivileged ISA specification](https://docs.riscv.org/reference/isa/unpriv/unpriv-index.html), for RV32I behavior and custom encoding space.
- [RISC-V assembly manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc) and [GNU `.insn` formats](https://sourceware.org/binutils/docs/as/RISC_002dV_002dFormats.html), for extension assembly without a new parser.
- [RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/), for program image conventions.
- [Accellera SystemC reference implementation](https://github.com/accellera-official/systemc), for simulation semantics.
- [Qiskit Aer](https://github.com/Qiskit/qiskit-aer), [NVIDIA cuStateVec](https://docs.nvidia.com/cuda/cuquantum/latest/custatevec/index.html), [QuTiP](https://qutip.readthedocs.io/en/latest/), and [Qiskit Dynamics archive](https://github.com/Qiskit-Community/qiskit-dynamics), for backend capability evaluation.
