# Glossary

This page defines the terms used in the simulator documentation.
For exact ordering and completion rules, use the
[control protocol](module-architecture.md).

## One example

Suppose the producer is preparing TCU cycle 4. Its **cursor** is 4.
Two APPENDs add actions to an **open group** for that cycle.
ADVANCE(3) **seals** the group and waits for the TCU to **admit** it.
After the reply reaches the CPU, the cursor becomes 7.

Meanwhile, the TCU follows its own timer. It **fires** the group at cycle 4,
provided the group was admitted before that cycle's edge. Moving the producer
cursor does not move the timer.

## Time and SystemC

| Term | Meaning |
| --- | --- |
| Host time / wall-clock time | Time spent running the simulator on a computer. A slow backend increases this time without changing simulated timestamps. |
| Simulation time / tick | Time in the modeled system. A tick is 1 ns in the current implementation; a timestamp counts ticks from simulation start. |
| Clock domain | State transitions driven by the same modeled clock. CPU and TCU clocks can have different periods and phases. |
| CPU cycle | A step of the CPU timing model at a CPU rising edge. |
| TCU logical cycle | The cycle relative to TCU start: cycle n occurs at `start + n * period`. It is calculated from the current tick. |
| Due edge | The TCU edge at which a group's planned cycle is reached. |
| Timing profile | Fixed configuration for clocks, capacities, mappings and delays. It is validated before construction and preserved across session resets. |
| Process | A SystemC scheduling unit such as `SC_METHOD` or `SC_THREAD`. It runs when its event or wait condition is satisfied. |
| `SC_METHOD` | A process that runs to return and cannot suspend with `wait()`. State needed on the next call belongs in persistent objects. |
| `SC_THREAD` | A process that can suspend at `wait()` and resume with its stack and local variables. It is not necessarily an OS thread. |
| Event / notification | A wakeup mechanism. `sc_event` carries no command payload; mailboxes retain messages separately. |
| SystemC channel | A communication object such as `sc_signal`. A primitive-channel update can emit an event that wakes a process. |
| Evaluation and update | Kernel phases that run ready processes and apply requested primitive-channel updates. |
| Delta cycle | Another scheduling round at the same simulation time. It is not a CPU or TCU cycle. |
| Physical boundary | A tick at which a device action starts or ends, a measurement is sampled, or a result becomes ready. |
| Barrier | The check that all clocked models due at a tick have finished before the device runtime processes that tick. |
| Old state / committed state | State visible at the start of a transition. For example, TCU admission uses queue occupancy before that edge's firing. |

## Programs and CPU execution

| Term | Meaning |
| --- | --- |
| RV32I | The base 32-bit RISC-V integer ISA. Quantum control uses additional custom encodings. |
| ISA / instruction semantics | What an instruction does to registers, memory or control flow, independent of pipeline timing. |
| Microarchitecture / CPU cycle model | The timing model for stages, hazards, stalls and retirement. |
| ELF / program image | A linked executable and the memory bytes, segment permissions and entry address loaded from it. |
| Retirement | Final completion of an instruction's architectural effect. It does not imply that a staged quantum action has already fired. |
| Oldest instruction | The earliest instruction still awaiting completion. Only it may publish a store or quantum-control effect. |
| Pipeline flush | Discarding younger instructions, for example after a taken branch. This differs from producer FLUSH. |
| Quantum instruction adapter | The function that converts a decoded custom instruction and its operands into a producer operation. |

## Command preparation and TCU

| Term | Meaning |
| --- | --- |
| Producer | The CPU-side object that collects actions and submits planned groups to the TCU. |
| Producer cursor | The logical TCU cycle currently being prepared. It is independent of the running TCU cycle. |
| Open group / staging | Bounded CPU-side storage for actions planned at one cursor position. Further APPENDs can add members. |
| Sealed group / submission | A group whose label, interval and members are fixed while it awaits admission. At most one is pending in this implementation. |
| Timing point | A group's interval, label and exact event-ID manifest. |
| Label | An increasing group identity used to match timing and event queues. It is not a cycle number. |
| Interval | TCU cycles since the preceding submitted point, or since logical zero for the first point. Intervals accumulate across empty queues. |
| Member manifest | The exact event IDs expected in a group. Per-port counts are computed from the group's events. |
| Producer acceptance | APPEND has secured staging and any result slot. The trace records `ProducerAccepted`. |
| Atomic admission | One timing entry and all its port events enter the TCU together, or none do. The trace records `GroupAdmitted`. |
| Group reply | `GroupReply` acknowledges an admitted label to the CPU through a return mailbox. Firing need not wait for this reply to reach the CPU. |
| Crossing | A mailbox transfer with receiver-edge latency. First eligibility is strictly after publication. |
| Backpressure | A pending request waits for temporarily occupied storage. A group that can never fit causes a capacity fault. |
| Timing queue | The TCU FIFO of timing points and cumulative due cycles. |
| Per-port event queue | A TCU FIFO containing resolved actions for one physical port. |
| Queue capacity | How many entries a queue can retain across multiple groups. |
| Firing width | How many actions one port may contribute to a single group. |
| Firing / label broadcast | Selecting the due label and releasing its validated event group on a TCU edge. |
| Launch preflight | Checking conditions, action support and physical reservations before committing a launch. |
| Wait-only point | A real timing entry with an empty event manifest. |
| Empty timing queue | No point is currently queued. The timer continues running and can accept later work before its deadline. |
| Empty stream | A stream closed without admitting any group. Its EndOfStream marker has label zero. |
| APPEND / ADVANCE / FLUSH | Add actions at the cursor; seal and move to a later cursor; or seal without moving. ADVANCE(0) does neither. |
| Nondeterministic preparation | Preparation whose latency can vary, for example because the CPU stalls. The term does not imply random simulation behavior. |

## Devices and feedback

| Term | Meaning |
| --- | --- |
| Port / codeword | A configured command destination and the digital value selecting its action mapping. |
| Action descriptor | A fixed description of operation kind, output port, targets, resources, delays and duration. |
| Device output channel | A modeled physical control or readout path. It is distinct from a SystemC channel. |
| Resource calendar | Future reservations over half-open intervals `[start, end)`. Adjacent intervals can share an endpoint. |
| Physical batch | All physical actions processed at one tick after clocked transitions have finished. |
| Quantum backend | The replaceable implementation that evolves shared quantum state and supplies measurement bits. |
| Backend capability | An action supported by an adapter, such as gates or joint pulse evolution. Unsupported requests fail validation. |
| Acquisition | A timed readout interval. The current model samples and collapses state at its end. |
| Discriminator arm | The tick at which processing is enabled. Result readiness is `max(acquisition_end, arm_start) + delay`. |
| Measurement token | Full identity of a measurement: epoch, measurement ID, slot, generation, handle and target. |
| Measurement handle | The 32-bit reference returned to the program by a measurement APPEND and later passed to QREAD. |
| Scoreboard | Bounded CPU result slots that move from Free to Pending to Visible, then back to Free on consumption. |
| CPU visibility | The result has crossed into its CPU slot. It can now complete QREAD. |
| Fast-condition history | Separate bounded TCU storage of exact-token results for QAPPEND_IF. Newly committed results affect later TCU edges. |
| Delivery credit | Reserved capacity for a measurement path. Fast credits return after TCU delivery, independently of CPU slot consumption. |
| Drain | Completion of all queued work and enabled deliveries after END closes production. Unread Visible slots may remain. |
| Session reset / epoch | Reset clears mutable controller and backend state and increments the epoch, while preserving memory and configuration. |
| Slot generation | A reuse counter for one result slot. It survives reset so old handles cannot become valid again. |
| Trace event | A timestamped observation such as retirement, firing or result visibility. Logging does not advance simulation time. |

## Related architectures

| Term | Meaning |
| --- | --- |
| QuMA-style TCU | Timing and event queues separate variable preparation time from deterministic output. |
| Distributed-HISQ | An architectural reference for RISC-V control extensions and port/codeword operations. Binary compatibility is not claimed. |
| CACTUS comparison | Independent execution of equivalent workloads followed by comparison of corresponding timed events. The tools remain outside this repository. |
| TQEC integration | A future adapter for compiling TQEC workloads and consuming their results. |
