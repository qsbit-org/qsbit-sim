# High-Level Design

**Status:** Discussion draft, 2026-09-16

## 1. Purpose and first milestone

qsbit-sim is a timing-aware simulator for a classical processor that schedules quantum control events and reacts to measurement results. The first milestone executes an RV32I program, issues timing instructions to a QuMA-inspired timing control unit (TCU), sends precisely timed control events to a mock quantum device, and resumes program execution after measurement feedback. The same run must produce a machine-readable event trace that explains delays and stalls.

The first milestone is a control architecture simulator. It does not model analog pulses, quantum state evolution, error correction, or a full QuMA instruction set. Those are future device adapters and workload layers.

## 2. Design principles

1. **Separate instruction semantics from timing.** The RV32I instruction decoder and architectural state transitions are pure C++20. A pipeline model adds cycle timing. SystemC coordinates modules, channels, clocks, and events.
2. **Keep the CPU replaceable.** The TCU consumes a versioned command protocol, not pipeline registers or a particular simulator's object model. A future external RISC-V engine can emit the same commands and receive the same feedback.
3. **Separate issue time from execution time.** The CPU may issue a command before its target timestamp. The TCU queues it and releases it according to a simulation clock. This follows QuMA's queue-based precise event timing principle, rather than CACTUS source structure.
4. **Make pressure visible.** Bounded command queues, output lanes, feedback paths, and stalls have explicit capacities and trace events. Event timing is expressed in a single documented tick unit.
5. **Preserve reproducibility.** Inputs, configuration, random seeds, stop reason, and every externally visible timed event are recorded.

## 3. Proposed component boundaries

```text
Program image -> RV32I ISA core -> cycle-level pipeline -> command adapter
                                                    |             |
                                         memory and MMIO bus      v
                                              TCU command port -> TCU queues and scheduler
                                                                   |               ^
                                                                   v               |
                                                        device event port     feedback port
                                                                   |               ^
                                                                   v               |
                                                          device model -----------+
```

The ISA core owns register semantics, program counter updates, instruction decoding, and exceptions. The pipeline owns fetch, decode, execute, memory, and retirement timing, including hazards and stalls. Memory and MMIO are accessed through an explicit bus interface. The command adapter translates architecturally visible writes or custom control instructions into `TimedCommand` values. The TCU owns event queues, timestamp checks, resource conflicts, dispatch, and feedback routing. The device model owns the behavior that produces measurement data and may be replaced by a circuit-level or pulse-level simulator.

The initial proposal uses memory-mapped command registers and a doorbell to avoid inventing a RISC-V ISA extension in the first milestone. This is a design choice for discussion, not a claim about QuMA's instruction encoding. An instruction-extension adapter can later produce the same `TimedCommand` values.

## 4. Stable ports and data contracts

The public CPU-facing boundary should be plain C++20, with no SystemC types:

- `CpuStepResult`: architectural retirement record, memory request, optional TCU command, exception, and cycle count.
- `TimedCommand`: command ID, operation kind, target resource, target tick, payload identifier, and correlation ID.
- `Feedback`: correlation ID, measurement value, availability tick, and status.
- `MemoryAccess`: address, width, byte-enable mask, read or write data, and completion status.
- `TraceEvent`: event kind, simulation tick, source, correlation ID, and typed details.

The SystemC adapter converts these values to timestamped transactions and waits on clock and completion events. Version the serialized trace and test fixture formats. Do not use a C++ ABI boundary as the promised external integration contract; another simulator may need a C-compatible adapter or an out-of-process trace protocol.

The first TCU model has one command queue, one output lane, a monotonically increasing tick counter, and one feedback queue. Queue capacity and output occupancy are configuration values. Each accepted command has a target tick. The TCU rejects commands in the past, reports capacity stalls, and records actual dispatch time. A later revision can add multiple queues and lanes without changing command identity or feedback correlation.

## 5. Execution and timing semantics

At each simulation tick, the CPU pipeline advances as permitted by memory and command-port backpressure. A retired command write can enter the TCU queue only after it is architecturally committed. The TCU dispatches a command when its target tick arrives and the addressed output resource is available. A measurement command causes the device model to return feedback after a configured delay. Software polls a status register in the first milestone; interrupt-based feedback is a later extension.

The simulator must report separately: instruction issue and retirement ticks, command enqueue tick, requested target tick, actual dispatch tick, measurement completion tick, feedback availability tick, and software consumption tick. A delayed output is not silently relabeled as on time. Define whether late commands are rejected or dispatched late before implementation; the current recommendation is to reject an already late command and trace the reason.

The SystemC time resolution should be configured once at startup. The TCU tick duration and CPU clock period are explicit configuration parameters. Where they differ, adapters translate time using checked integer arithmetic and a documented rounding rule. Host execution time is a performance metric, never simulated device time.

## 6. Extensibility path

- **CPU backends:** `InternalRv32iPipeline` first; later `ExternalCpuAdapter` for a mature RISC-V simulator. Both must produce equivalent architectural retirement and MMIO transactions for the same supported program. Exact internal cycle behavior need not match unless the backend declares the same timing model.
- **Instruction front ends:** MMIO first; a custom quantum instruction decoder can be added behind the command adapter.
- **Device backends:** deterministic scripted device first; circuit-level and pulse-level adapters later. A backend declares supported operations and timing fidelity.
- **Control feedback:** configurable scripted latency first; a real decoder feed later, with a separate arrival-time field and backlog accounting.
- **TCU scale:** multiple queues, lanes, resource conflicts, and synchronization points after the single-lane baseline is verified.

## 7. First milestone acceptance scenarios

1. A hand-written RV32I program emits two timed events. The trace proves their target and actual dispatch ticks, order, and retired source instructions.
2. A measurement event produces a delayed feedback value. The program observes it and takes one of two branches. Both outcomes are tested.
3. A full command queue stalls the CPU-visible command write. The trace and final register state show that no command is lost or duplicated.
4. A command with a past target tick produces the documented error and never reaches the device.
5. A load-use dependency and a taken branch exercise pipeline stalls and flushes while producing the same final architectural state as an ISA reference model.

## 8. Decisions to settle before implementation

1. Is the first pipeline a conventional five-stage, single-issue in-order model? This draft recommends yes, with configurable stage latency only where it reflects an explicit component.
2. Is MMIO the initial software-visible TCU interface? This draft recommends yes. We should freeze the register map and commit semantics before coding.
3. What is the authoritative unit for TCU target timestamps, and what should happen when a command arrives late? This draft recommends integer ticks and explicit rejection.
4. How many independent command queues and output lanes are essential in the first demonstrator? This draft recommends one of each, while preserving multi-lane fields in the protocol.
5. Which timing reference defines the first milestone's acceptance trace: a small written schedule or a specific QuMA example? The written schedule is easier to make deterministic; a QuMA example can follow as a separate comparison.

## 9. Primary references

- Xiang Fu, [*Quantum Control Architecture: Bridging the Gap between Quantum Software and Hardware*](https://research.tudelft.nl/en/publications/quantum-control-architecture-bridging-the-gap-between-quantum-sof/), TU Delft, 2018. The thesis identifies codeword-based event control, queue-based precise event timing, and multilevel instruction decoding as QuMA principles.
- [RISC-V Unprivileged ISA specification](https://docs.riscv.org/reference/isa/unpriv/unpriv-index.html). Normative source for RV32I semantics.
- [Accellera SystemC standards and reference implementation](https://systemc.org/resources/standards/). Normative SystemC starting point.
