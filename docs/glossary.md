# Glossary

This glossary explains the terms used in the [high-level design](high-level-design.md), [architecture and shared timing protocol](module-architecture.md), and [module contracts](modules/README.md). It describes the proposed qsbit-sim baseline. Exact instruction encodings and numerical timing-profile values have not yet been selected.

## Start with one example

Suppose the CPU-side **producer cursor** is at logical TCU cycle 4. `APPEND(A)` and `APPEND(B)` put two operations into an **open group** for that cycle. `ADVANCE(3)` **seals** the group and waits until the TCU **admits** all of its entries; it then moves the producer cursor to cycle 7. The TCU **fires** the group when its own timer reaches cycle 4, provided admission happened before that cycle's due edge. These are different milestones: staging, admission, and firing do not occur simply because the producer cursor changes.

## Time and SystemC

| Term | Meaning in this design |
| --- | --- |
| **Host time / wall-clock time** | Time spent running the simulator on a computer. A slow backend call may increase host time without advancing simulated hardware time. |
| **Simulation time / global tick** | The kernel's modeled time, represented on one configured integer grid. A tick is the grid unit; a timestamp is a position on that grid. CPU edges, TCU edges, and physical boundaries have global ticks. |
| **Clock domain** | A set of state transitions driven by one modeled clock. The CPU and TCU may have different periods and phases. |
| **CPU cycle** | One step of the selected CPU pipeline timing model. It is not necessarily one TCU cycle. |
| **TCU logical cycle / `T_D`** | The deterministic-domain timer's current cycle count. With start tick `S` and period `P`, running cycle `n` occurs at global tick `S+nP`. `T_D` is live TCU state, unlike the producer cursor. |
| **Edge / due edge** | A configured clock transition at which an owner steps. A point's due edge is the TCU edge on which its planned logical cycle is reached. |
| **Timing profile** | The validated configuration of modeled periods, phases, crossing latencies, capacities, port widths, output delays, acquisition timing, and backend capabilities. It is immutable within a simulation session; live counters and queue contents still change. A new profile requires a separately identified session. |
| **SystemC process** | A kernel-scheduled `SC_METHOD`, `SC_THREAD`, or `SC_CTHREAD`. It executes when its sensitivity or wait condition is met. A logical module in these documents need not be its own process. |
| **`SC_METHOD` / `SC_THREAD`** | A method runs to return without `wait()`; a thread can suspend at `wait()` and later resume. Neither is automatically a separate operating-system thread. |
| **SystemC event / notification** | A wakeup mechanism for waiting processes. An event is not itself a command payload; our cross-domain mailboxes retain payloads and identities separately. |
| **SystemC channel** | A communication object such as `sc_signal` or `sc_fifo`. A primitive channel can request an update; a resulting channel event may wake a process. This is distinct from a **device output channel**, which models a physical control or readout path. |
| **Evaluation, update, and delta cycle** | The kernel evaluates runnable processes, applies requested primitive-channel updates, and may run another zero-time scheduling round. Multiple delta cycles can occur at one global tick. A delta cycle never counts as a CPU or TCU hardware cycle. |
| **Timed event / physical boundary** | Work scheduled for a future simulation tick, such as a pulse start, pulse end, acquisition end, or result-ready event. SystemC can jump to its next scheduled time when no current-time work remains. |
| **Committed state / old state** | The state visible at the start of an owner's transition. Same-edge admission checks old queue occupancy; a slot freed by firing on that edge becomes available for admission only on a later edge. |

## CPU and program

| Term | Meaning in this design |
| --- | --- |
| **RV32I** | The base 32-bit RISC-V integer instruction set executed by the initial classical engine. Quantum-control operations come from versioned custom extensions. |
| **ISA / instruction semantics** | The meaning of a decoded machine instruction: operands, register and control effects, and faults. The ISA library does not decide how many pipeline cycles execution takes. |
| **Microarchitecture / CPU cycle model** | The implementation timing model for fetch, hazards, stalls, memory responses, and retirement. Another CPU backend may replace it if it satisfies the same boundary contract and timing tests. |
| **ELF / program image** | The linked RV32 machine-code file and its loadable segments and entry address. qsbit-sim loads machine code; a separate assembler or linker handles assembly text. |
| **Retirement / commit** | The point at which an instruction's architectural effect becomes final. Only an authorized, oldest non-speculative instruction may publish an irreversible external control action. |
| **Quantum-instruction adapter** | Converts an authorized extension instruction into a producer operation such as `APPEND`, `ADVANCE`, or `READ_RESULT`. These names specify behavior; their binary encodings remain open. |

## Producer and TCU

| Term | Meaning in this design |
| --- | --- |
| **Producer** | The CPU-side timeline manager that prepares ordered timing points and port events before TCU admission. Preparation may have variable latency. |
| **Producer cursor** | The TCU logical cycle currently being *planned* by the producer. It is neither the current CPU cycle nor the TCU timer's live `T_D`. After a successful `ADVANCE(3)` from cursor 4, it becomes 7. |
| **Open group / staging** | A bounded CPU-side collection of operations planned for one producer-cursor position. Multiple `APPEND` operations may join it. It has not entered TCU queues. |
| **Seal / sealed group / frozen submission** | To fix the open group's label, interval, member list, and action descriptors for submission. The group cannot change while crossing or waiting for admission. The baseline permits at most one sealed submission awaiting a reply. |
| **Timing point / label** | A scheduled position in the producer's logical timeline and its identity tag. A label lets the timing queue and each relevant per-port queue refer to the same group; it is not itself an absolute SystemC timestamp. |
| **Interval** | The number of TCU logical cycles from the preceding sealed point to the next one. A cumulative interval determines a due cycle; admission time does not shift that due cycle. |
| **Member manifest** | The timing point's exact list of expected event IDs and per-port counts. It lets the TCU check that all members are present before any member fires. An empty manifest is a valid wait-only point. |
| **`ProducerAccepted`** | Acknowledgment that an `APPEND` event and any required measurement slot have entered bounded CPU-side staging. It does not mean that the TCU has accepted or fired the group. |
| **`GroupAdmitted` / atomic admission** | Acknowledgment that one sealed timing entry and all its required per-port event entries were inserted together. If any required check fails, none is inserted. Admission is earlier than firing. |
| **Crossing / receiver-edge latency** | Transfer between clock-domain owners through a committed mailbox. A message published at tick `p` is first eligible on a receiver edge strictly after `p`; configured latency `N >= 1` counts receiver edges from there. It does not rely on incidental delta-cycle order. |
| **Backpressure** | A request remains pending because a bounded resource is temporarily full. An intrinsically oversized group instead produces a typed error; waiting cannot make it fit. |
| **Timing queue** | Bounded TCU FIFO of ordered intervals, labels, and manifests. It preserves planned logical timing rather than rescheduling a point from its arrival tick. |
| **Per-port event queue** | Bounded TCU FIFO for actions directed to one configured output port. The timer's label broadcast selects the expected heads across all relevant queues. |
| **Firing / label broadcast** | On a due TCU edge, the timer announces the label and the TCU checks the complete matching group for launch. A group admitted on that same edge cannot fire then. |
| **Launch preflight** | Checks the full due group, conditions, supported actions, and physical resource reservations before any physical side effect. A fatal conflict rejects the whole batch. |
| **Empty stream / wait-only point** | An empty queue means no currently admitted point is ready; the TCU timer continues. A wait-only point is an actual admitted timing entry with no port events, used to preserve an intentional interval. |
| **`APPEND`, `ADVANCE`, `FLUSH`** | Proposed producer operations: add an event to the open group; move the planned cursor after sealing a real open group; or seal without moving the cursor. `ADVANCE(0)` neither seals nor moves. See [the exact completion rules](module-architecture.md#41-producer-operations-and-progress). |

## Device actions and feedback

| Term | Meaning in this design |
| --- | --- |
| **Port / codeword** | A port names a configured control or readout destination; a codeword selects an action in that port's immutable map. Neither prescribes a particular quantum gate or waveform in every profile. |
| **Resolved action descriptor** | The action selected from the port and codeword before admission, including kind, physical resources, output delay, duration, and backend requirements. It stays fixed for the submitted group. |
| **Device output channel / resource calendar** | A modeled physical control path and DeviceRuntime's reservations for its future half-open occupancy intervals `[start,end)`. Future reservations matter even before a pulse starts. |
| **Physical batch / same-tick ordering** | All physical actions assigned one global tick are validated together, then processed in the specified order. A port's iteration order must not change the quantum result. |
| **Quantum-state service / backend adapter** | One chronological owner calls a replaceable state or pulse simulator for the shared quantum state. Ports do not independently advance the same backend. Backend host runtime does not determine feedback visibility. |
| **Acquisition / discriminator** | Acquisition collects a readout over a modeled interval; the discriminator turns the sampled outcome into a result after its arm and processing delay. A backend measurement outcome is not immediately a CPU-visible result. |
| **Measurement token / scoreboard** | An epoch-tagged identity and bounded CPU-side slot allocated when a measurement APPEND is accepted. The scoreboard tracks that exact result from pending to CPU-visible and finally consumed. |
| **CPU feedback crossing / `CpuResultVisible`** | The discriminator's tagged completion reaches the CPU only after its configured receiver-edge latency. `READ_RESULT(token)` first flushes its open group, then waits for and consumes that token's visible result. |
| **Fast-condition history** | An optional, separate TCU-side result path for conditional output. It can have different latency from CPU feedback; a due action reads the previously committed condition snapshot. |
| **`END` / drain** | Closes producer input after any required seal and admission. Successful simulation completion waits for queued points, physical actions, and all enabled feedback deliveries to finish; CPU halt alone is insufficient. |
| **Session reset / epoch** | A global simulation-session boundary. Reset clears controller and backend state, increments an identity generation, and discards old-epoch callbacks without rewinding SystemC time. It is not a modeled physical controller-only reset. |
| **Trace / boundary event** | A versioned observation of acceptance, admission, firing, physical action, result visibility, or fault with identity and tick. Trace collection cannot change hardware timing. |

## Project scope and comparison

| Term | Meaning in this design |
| --- | --- |
| **QuMA-style TCU** | The queue-based separation of variable-latency preparation from deterministic label-triggered output. This project keeps that timing principle without adopting eQASM binary encodings. |
| **Distributed-HISQ inspiration** | Motivation for RV32I control extensions, port/codeword operations, and a future synchronization boundary. Binary compatibility and multi-node synchronization are not Phase 1 claims. |
| **CACTUS differential gate** | An external validation project runs equivalent workloads independently on CACTUS and qsbit-sim and compares normalized quantum-control boundary events at zero common-grid-tick tolerance. Its comparison code is outside this repository. |
| **Quantum backend capability** | A declared behavior such as ideal-gate stepping, mid-circuit collapse, or joint pulse evolution. A workload requiring an unsupported capability fails explicitly. |
| **TQEC integration** | A later Phase 2 adapter from TQEC workloads to the simulator's program, measurement, and trace contracts; it does not define Phase 1 timing. |

For exact requirements, use [Section 4 of the architecture document](module-architecture.md#4-baseline-protocol-and-event-ordering). The glossary explains the vocabulary; it does not replace the protocol or each [module contract](modules/README.md).
