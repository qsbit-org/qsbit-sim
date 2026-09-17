# Architecture overview

qsbit-sim models a programmable quantum controller. A classical CPU runs RV32I
machine code with quantum-control extensions. A timing control unit (TCU)
buffers the requested operations and releases them at their planned times.
A device model then accounts for output delays, acquisition and measurement
feedback.

This separation lets the CPU take a variable number of cycles to prepare
commands while the device output follows a fixed timeline. It also gives the
CPU model and quantum backend independent replacement interfaces.

## Preparing commands ahead of time

Consider two operations that should start at TCU cycle 8. The CPU executes
instructions to select their ports and codewords. The producer collects both
operations into an **open group** for cycle 8. A later instruction seals the
group, fixing its contents for submission.

The TCU admits the complete group into its timing and event queues. When its
timer reaches cycle 8, it checks the group and fires the selected actions
together. Device-specific delays determine the physical start of each action.
The group must arrive before its due edge; a late command is an error.

This is the reservation-and-trigger mechanism used by QuMA. qsbit-sim applies it
to RV32I control instructions. Its classical pipeline and binary encodings are
defined by this project.

## Main components

The [clickable architecture diagram](architecture.md) shows all logical modules.
The main path through the controller is:

```text
Program image -> CPU -> Timeline producer -> TCU -> Device runtime -> Quantum backend
                  ^                                  |
                  +------- measurement feedback -----+
```

| Component | Responsibility |
| --- | --- |
| Program image and memory | Load ELF segments and service timed instruction and data accesses. |
| ISA library | Decode RV32I and custom instructions and calculate architectural effects. |
| CPU cycle model | Advance the pipeline, handle stalls and branches, and retire instructions. |
| Timeline producer | Resolve port/codeword commands and assemble bounded groups for future TCU cycles. |
| TCU | Admit complete groups, preserve their planned times, and fire labels on TCU edges. |
| Device runtime | Reserve physical resources, schedule output and readout, and publish results. |
| Quantum backend | Evolve shared quantum state and return measurement outcomes. |
| Simulator | Connect these models to SystemC clocks, timed events, reset and completion. |

These are logical responsibilities. Several belong to one C++ object.
`Simulator` is the SystemC module; a queue or helper function does not add a
clock stage just because it appears as a box in the diagram.

## SystemC scheduling

SystemC maintains simulation time and runs processes when their events occur.
The CPU and memory methods run at CPU rising edges; the TCU method runs at TCU
rising edges. The device runtime runs at physical boundaries such as pulse
starts, acquisition ends and result-ready ticks.

At a tick shared by several clocks, each clocked model completes its transition
before the device barrier processes physical actions. Messages carry an explicit
receiver-eligible tick. These rules make hardware-visible behavior independent
of which runnable SystemC process executes first.

Delta cycles coordinate work without advancing simulation time. Instruction
latencies, queue capacities and output delays come from the C++ models and
timing profile. The [control protocol](module-architecture.md) defines their
ordering rules.

## Programs and instruction extensions

The simulator loads ELF or raw machine code. GNU RISC-V assembler macros encode
the current custom instructions with `.insn`; there is no assembly-text parser
inside the simulator.

The extensions append a port/codeword action, advance the planned TCU cycle,
flush a group, read a measurement, close the stream or attach a fast condition.
QSYNC has a reserved encoding but raises `UnsupportedSynchronization`.
The [instruction reference](interfaces.md#quantum-instruction-encoding) specifies
the fields and completion behavior.

The ISA library and CPU timing model are separate. A different pipeline can
reuse instruction semantics, and a different instruction adapter can produce
the same control operations. The current CPU has three in-order stages.

## Measurement feedback

The device runtime samples a measurement at acquisition end. Discriminator
timing determines when the bit is ready. Separate crossings deliver it to
the CPU result slots and, when enabled, TCU fast-condition history.

QREAD waits for the CPU-visible result and consumes its handle. A program can
then branch and prepare a future group. QAPPEND_IF instead attaches an exact
measurement token to an action; the TCU tests that token at firing time.
Both paths preserve the existing TCU timeline.

## Replacing a model

`ICpuCycleModel` defines CPU stepping, reset and architectural state access.
An adapter must preserve instruction completion and prevent speculative
instructions from publishing control effects. Matching final register state
alone does not establish timing equivalence.

`IQuantumBackend` defines validation, reset, interval evolution, gate batches,
measurement and optional state inspection. One device runtime calls the backend
for the shared state. The backend's host computation time never changes simulated
latencies. See [backend integration](backends.md) and [C++ interfaces](cpp-interfaces.md).

## Scope and validation

The current implementation supports RV32I, the custom-0 control profile,
bounded control queues, CPU and fast measurement feedback, and scripted, Aer
and constant-pulse backends. [Implementation reference](implementation.md)
lists the numerical defaults and limits.

CACTUS comparisons check corresponding quantum-operation and supported feedback
times after each simulator executes its own program. The comparison tools remain
in a separate disposable project. Different classical pipelines need not have
matching instruction counts or private queue occupancy.
[ADR 0002](decisions/0002-reference-comparison-scope.md) defines the comparison
limits.

TQEC input, distributed synchronization and additional numerical adapters are
future integrations.

## Architecture references

- [QuMA](https://arxiv.org/abs/1708.07677), Sections 5.1–5.3: timing and event queues,
  label broadcast, codeword-triggered output and readout.
- [eQASM](https://arxiv.org/abs/1808.02449), Sections 3.1 and 3.4–3.6: reservation,
  parallel operations and feedback.
- [Distributed-HISQ](https://arxiv.org/abs/2509.04798), Sections 3–4: RISC-V control
  extensions, port/codeword operations and distributed synchronization.

The timing mechanism follows these architectures; qsbit-sim does not claim
binary compatibility with eQASM or Distributed-HISQ.
