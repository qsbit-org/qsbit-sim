# Glossary

Read [Simulation time and execution](simulation-model.md) for the scheduling model.
The [control protocol](module-architecture.md) specifies ordering and completion.

## Time and scheduling

### SystemC

A C++ library for discrete-event simulation. Its kernel maintains simulation time and schedules processes. qsbit-sim connects ordinary C++ models to that kernel through one `Simulator` module.

### Kernel and scheduler

The kernel runs ready processes, applies requested channel updates and delivers notifications. When no work remains at the current simulation time, it advances to the next scheduled time. A process returns or suspends before another process runs.

### Event-driven simulation

Execution in response to scheduled events, such as a clock edge or an action boundary. Event-driven scheduling can support a cycle-level model.

### Cycle-level model

A model that specifies state changes and latencies at clock edges. qsbit-sim models its own CPU pipeline and control protocol; its cycle counts do not claim equivalence to a particular commercial processor.

### Host time

Elapsed time spent running the simulator on a computer, also called wall-clock time. Backend computation can increase host time without changing simulation time.

### Simulation time

Time in the modeled system. It advances under kernel scheduling and continues across session resets.

### Tick

The simulation time unit: 1 ns. A timestamp counts ticks from simulation start. The C++ alias `Tick` also stores some cycle counts; consult the [field units](cpp-interfaces.md#time-and-cycle-units) before interpreting a value.

### Clock period

The time between successive rising edges of one clock.

### Clock phase

The offset of the first rising edge from global time zero. Subsequent edges occur at `phase + k * period`, for non-negative integer k.

### Rising edge

A modeled clock transition from low to high. A receiver edge belongs to the clock of the component receiving a message.

### Clock domain

State transitions governed by one modeled clock. CPU and memory share the CPU clock; TCU transitions use the TCU clock.

### CPU edge index

The zero-based index `(tick - cpu.phase) / cpu.period` at a CPU rising edge. It continues across session resets. A CPU cycle is the interval between consecutive CPU edges.

### TCU logical cycle

The planned or current cycle relative to the effective start of the current epoch. Cycle n is due at `epoch_start + n * tcu.period`. The producer cursor names a planned cycle; the TCU computes its current cycle from time.

### TCU start

The initial `profile.start` is a global tick on a TCU edge. After reset, it is used as an offset: the effective epoch start is the first TCU edge at or after `reset_tick + profile.start`.

### Due edge

The TCU edge when a queued group must fire. Admission must occur strictly before that edge.

### Physical boundary

A tick containing an action start, action end, measurement sample or result-ready event. It need not coincide with a clock edge.

### Delta cycle

A scheduling round at the same simulation time. Additional delta cycles do not consume CPU cycles, TCU cycles or nanoseconds.

## SystemC processes and model state

### Logical module

A responsibility shown in an architecture diagram. Several logical modules can share a C++ object; a diagram box does not imply a process or pipeline stage.

### State owner

The C++ object responsible for changing a particular state. Ownership does not imply a thread, clock domain or communication delay.

### SystemC module

An `sc_module` that contains registered processes and connections. `Simulator` is the qsbit-sim module; it calls ordinary C++ model objects.

### Process

A kernel scheduling unit. qsbit-sim registers five `SC_METHOD` processes: CPU edge, memory edge, TCU edge, timed wakeup and device barrier.

### Elaboration

Construction of modules, processes and connections before simulation starts.

### Initialization

The initial kernel scheduling phase. `dont_initialize()` suppresses the automatic initial invocation of a method; a triggering event at time zero can still run it.

### Sensitivity

The events that make a process runnable. Static sensitivity is declared when the process is registered; qsbit-sim uses clock events and its wakeup and barrier events.

### SC_METHOD

A process that returns after handling its trigger and cannot suspend with `wait()`. Persistent model objects retain state for the next invocation.

### SC_THREAD

A process that can suspend at `wait()` and resume with its stack and local variables intact. It is not an operating-system thread; qsbit-sim uses methods for its model scheduling.

### SystemC wait

Suspension of a thread process until a time or event condition is met. A modeled CPU stall instead retains an unfinished instruction and retries on a later CPU edge.

### SystemC notification

A notification makes processes sensitive to an `sc_event` runnable. `notify()` is immediate, `notify(SC_ZERO_TIME)` targets a later delta at the same time, and a positive delay schedules a future time. Notification does not call the receiving method inline.

### SystemC event

An `sc_event` carries no payload and has at most one pending notification. Timed notifications do not form a message queue. The model retains data separately; cancelling a notification does not undo work already delivered.

### SystemC channel

A communication object such as `sc_signal`. A primitive channel participates in the kernel update phase; this mechanism is separate from qsbit-sim mailbox storage.

### Evaluation and update

During evaluation, ready processes run. During update, primitive channels apply requested changes. A signal write is committed in update; ordinary C++ model assignments take effect according to that model's code and transition contract.

### Device barrier

The method that checks whether all clocked transitions due at the current tick have finished, then processes device work. It uses a next-delta notification and completion markers; it does not block in `wait()`.

### State transition

One model invocation that reads inputs, validates proposed changes and commits its state. Several checks within a transition do not imply several clock cycles.

### Edge-entry state

State at the beginning of a model's clock-edge transition. TCU capacity checks use this occupancy, before same-edge firing frees entries.

### Committed state

State after a model applies validated changes. Visibility depends on the model: CPU operand capture can read a register retired earlier on that edge; TCU conditions use history committed on earlier edges.

### Atomic transition

A set of modeled changes that commits together after validation, or does not commit if validation fails. It describes model behavior, not a C++ atomic instruction or a thread lock. It does not roll back earlier retired instructions.

## Programs and CPU execution

### RV32I

The base 32-bit RISC-V integer instruction set. qsbit-sim adds custom-0 quantum-control encodings.

### ISA

The instruction set architecture: encodings and architectural behavior available to a program. Instruction semantics specify each instruction's effects independently of pipeline timing.

### Microarchitecture

The implementation model that determines stages, hazards and timing while carrying out the ISA.

### CPU cycle model

The replaceable implementation of CPU transitions at clock edges. The default model has fetch, decode and execute/commit stages.

### Architectural state

The program-visible registers, PC and memory effects. Pipeline latches and outstanding requests are internal model state.

### Program counter

The PC: the address identifying an instruction in the program. Retirement records contain the retired instruction's PC and its next PC.

### Pipeline stage

One part of instruction processing. Fetch requests instruction bytes, decode interprets the encoding, and execute/commit completes the instruction's effects.

### Pipeline latch

Stored information about an instruction between stages. Each latch advances at most one stage per CPU edge.

### Stall

A modeled delay that retains unfinished work. For example, a load holds execute until its memory response becomes available.

### Hazard

A dependency or resource conflict that constrains instruction progress. Reading a register before an older instruction writes it is a data hazard.

### Forwarding

Making an older instruction's newly committed value available to a dependent instruction. In this CPU model, retirement precedes the next instruction's operand capture on the same edge.

### Speculative instruction

Younger work fetched before earlier control flow or faults are resolved. A taken branch can discard it before it produces an architectural effect.

### Retirement

Final completion of an instruction's architectural effect. QAPPEND can retire while its device actions are still waiting for admission or firing.

### Oldest instruction

The earliest instruction still awaiting completion. Only it may publish a store or quantum-control effect.

### Pipeline flush

Discarding younger instructions after a branch or termination. Producer FLUSH instead seals a control group.

### Fetch generation

A CPU counter identifying valid outstanding fetch work. Changing it invalidates replies from a discarded instruction path; it is separate from measurement-slot generation.

### ELF

An executable file format containing loadable segments and an entry address. qsbit-sim accepts the documented RV32I ELF32 subset.

### Program image

The loaded memory bytes, segment permissions and entry address represented by `ProgramImage`. It can originate from ELF or raw machine code.

### Quantum instruction adapter

`adapt_quantum()` converts an ISA instruction and its captured register values into a producer operation. See the [instruction mapping](interfaces.md#instruction-and-producer-operation-names).

## Commands and communication

### TCU

The timing control unit. It admits prepared groups and releases them on their planned logical cycles.

### Timeline producer

`TimelineProducer`, the CPU-side object that stages actions and submits planned groups. It owns the producer cursor and is also called the producer.

### Producer cursor

The logical TCU cycle currently being prepared. Moving it does not move the running TCU timer.

### Producer operation

A request such as Append, Advance or ReadResult made by the CPU to the timeline producer. An incomplete request retains its identity and operands across retries.

### Control command

A source control port and codeword supplied by an append operation. Its mapping can expand into several action descriptors.

### Action descriptor

An `ActionSpec` defining kind, output port, targets, resources, delay and duration. It describes an action without assigning an execution identity.

### Queued control event

A `ReservedEvent`: a resolved action descriptor with an epoch, event ID, source instruction, label and optional tokens. TCU event queues store these records, not SystemC events.

### Group

A timing point and its resolved control events. Open and sealed describe producer states of this aggregate.

### Open group

The group being prepared at the cursor. Staging is the bounded CPU-side storage that retains its control events.

### Sealed group

A group with fixed label, interval and members, awaiting admission. A submission is the transfer of that group; at most one sealed group awaits acknowledgment.

### Timing point

The group's epoch, interval, label and exact event-ID manifest. A timing queue entry stores it together with its cumulative due cycle.

### Label

An increasing group identity that matches the timing point to its port events. It is not a cycle number.

### Interval

TCU cycles since the preceding submitted timing point, or since logical zero for the first point. Intervals accumulate across empty queues.

### Member manifest

The exact control-event IDs expected in a group. Per-port counts are derived from the events.

### Producer acceptance

An append operation has secured staging and any measurement result slot. The trace records `ProducerAccepted`.

### Admission

Atomic insertion of one timing entry and all its control events into TCU queues. The trace records `GroupAdmitted`.

### Group reply

A `GroupReply` acknowledging an admitted label through the return mailbox. Firing need not wait for CPU receipt of this reply.

### Mailbox

Bounded message storage with explicit receiver eligibility. A message remains stored until consumed. A notification only schedules work; it does not contain the message.

### Envelope

The mailbox record holding a payload, publication tick, eligible tick and epoch.

### Publication

Placing a message in its communication path with an eligibility timestamp.

### Eligibility

The earliest time a receiver may inspect or consume a message. Eligibility does not guarantee acceptance: capacity or other protocol checks can leave it pending.

### Consumption

Removal or use of a particular stored item. Mailbox consumption removes a message; QREAD consumption frees a CPU result slot.

### Crossing

A modeled mailbox transfer with latency measured in receiver edges. The first eligible edge is strictly after publication, even when clocks share an edge.

### Backpressure

A pending request waits for storage that earlier work can release. A permanently oversized group, or an exhausted CPU result array requiring a later QREAD, causes a fault.

### Timing queue

The TCU first-in, first-out queue of timing points and cumulative due cycles.

### Per-port event queue

A TCU first-in, first-out queue of control events for one physical output port.

### Queue capacity

The number of entries retained across groups. Queue occupancy is the number currently stored, not physical resource use.

### Firing width

The maximum number of events one physical output port contributes to one group.

### Firing

Releasing the validated group for its due label on a TCU edge, also called label broadcast. Physical actions can start later because of output delays.

### Launch preflight

Validation of the condition-selected actions against backend capabilities and physical reservations before committing a TCU launch.

### Launch batch

The `LaunchBatch` released by one firing. Different action delays can place its physical starts at different ticks.

### Wait-only point

A real timing entry with an empty event manifest. It retains a planned point on the timeline without launching an action.

### Empty timing queue

No timing entry is currently queued. The timer continues running and later work must still meet its original deadline.

### Empty stream

Production closed without admitting any group. Its `EndOfStream` marker has label zero.

## Devices and measurement results

### Source control port

Part of the mapping lookup key supplied by a control instruction. It need not equal the selected physical output port or a qubit index.

### Codeword

The digital value that, together with a source control port, selects an action mapping.

### Physical output port

The destination of a resolved action and the index of its TCU event queue.

### Memory access port

A request-and-response path for instruction fetch or data access. It is separate from device output ports.

### Qubit target

The backend quantum subsystem on which an action acts. A two-qubit gate names two ordered targets.

### Device output channel

A modeled control or readout path. It is separate from a SystemC channel and can reserve resources shared with other outputs.

### Resource

A configured conflict identifier used by physical reservations. A gate can reserve multiple resources, including resources shared by different output ports.

### Exclusive reservation

An interval during which another overlapping reservation cannot use the resource if either reservation is exclusive. Port occupancy is also checked independently.

### Resource calendar

Reservations over half-open physical intervals `[start, end)`. Adjacent intervals can share an endpoint; queue space and resource availability are separate constraints.

### Physical action

An identified device action with computed start and end ticks: `start = fire_tick + delay`, `end = start + duration`.

### Physical boundary batch

All physical work processed at one tick after coincident clocked transitions finish. It can contain work from several launch batches.

### Ideal gate

A matrix operation applied to quantum state at its physical start. Its positive duration reserves resources; it does not describe simulated pulse dynamics.

### Pulse drive

A control contribution active over an interval. The bundled pulse backend evolves under the sum of simultaneously active constant drives.

### State evolution

Numerical propagation of shared quantum state over an interval. All physical output ports use one backend state.

### Quantum backend

The replaceable implementation of quantum evolution and measurement. `IQuantumBackend` is the C++ interface.

### Python bridge

`PythonBackend`, the C++ implementation that calls a selected Python quantum backend. It is separate from that backend's numerical model.

### Backend capability

A supported action or operation, such as ideal gates or joint pulse evolution. Unsupported requested actions fail validation.

### Scripted backend

A backend returning configured bits indexed by measurement ID. It does not evolve quantum state.

### Numerical backend

A backend that calculates quantum state and measurement outcomes, such as Aer or the constant-pulse backend. These are simulated measurements, not a connection to laboratory hardware.

### Readout

The modeled path from acquisition through sampling and discriminator delay to result delivery.

### Acquisition

A timed readout interval. The current model samples and collapses quantum state at its end.

### Discrimination

Conversion of a readout signal to a measurement result in hardware. qsbit-sim models its delay; the backend supplies the bit directly, without raw-waveform classification.

### Discriminator arm

Enabling readout processing at a specified tick. Result readiness is `max(acquisition_end, arm_start) + discriminator_delay`.

### Measurement token

The full measurement identity: epoch, measurement ID, slot, generation, handle and target.

### Measurement handle

The 32-bit reference returned by an acquisition QAPPEND and passed to QREAD or QAPPEND_IF.

### Scoreboard

Bounded CPU result slots that move from Free to Pending to Visible, then back to Free when QREAD consumes the bit.

### Measurement completion message

A `Completion` carrying a token and bit on a result path. It does not indicate that the simulation has completed.

### Measurement result delivery

Transport of a result to the CPU or TCU history. Delivery occurs even when the program makes no conditional decision, as in Bell.

### CPU conditional feedback

The program consumes a result with QREAD, branches and prepares later control actions.

### TCU conditional output

QAPPEND_IF attaches a token and expected bit to an action. The TCU tests it at firing, bypassing a CPU branch. The fast path name does not guarantee earlier result delivery for every configuration.

### CPU result visibility

The bit has reached its scoreboard slot before the CPU step. QREAD can consume it on that edge if its other completion conditions hold.

### Fast-history commit

A completion inserted into TCU history at the end of its transition, recorded as `FastResultVisible`. Conditions can use it starting on a later TCU edge.

### Fast-condition history

Bounded per-target TCU storage of exact-token results. CPU consumption does not erase it; an absent or evicted token is an error rather than a false predicate.

### Predicate

A condition comparing an exact measurement token's bit with the expected value. A false predicate cancels its action; unavailable history faults.

### Delivery credit

Reserved capacity for a result path. Fast credits return after TCU delivery acknowledgment, independently of CPU result consumption.

## Lifecycle, configuration and records

### Closure

The producer stops accepting operations and publishes `EndOfStream` after the required flush. The TCU records closure when it receives that marker.

### CPU halt

The CPU stops issuing instructions. Device work and result deliveries can continue after QEND halts it.

### Drain

Completion of queued work, physical actions, memory transactions and enabled message deliveries after closure. Unread Visible CPU slots may remain.

### Simulation stop

Termination of SystemC execution, either after successful drain or because of a failure. `SimulationCompleted` denotes success.

### Session reset

Reset of controller and backend state while preserving memory bytes and the simulation profile. Simulation time continues; old work is invalidated.

### Epoch

The identity of a simulation session, incremented by session reset. Protocol records use it to distinguish old work from the current session.

### Slot generation

A reuse counter for a measurement slot. It survives session reset so reusing the slot cannot revive an old handle.

### Run configuration

The JSON selected by `--config`: program, backend, output paths and optional simulation profile overrides.

### Simulation profile

The fixed `Profile` of clocks, capacities, mappings and delays, also called the timing profile. `--profile` reads profile values, not a complete run configuration.

### Conan build profile

Compiler, platform and build settings used to provision dependencies. It is unrelated to the simulation profile.

### Configuration fingerprint

An FNV-1a identifier of the simulation profile, not a cryptographic integrity check. `Group.configuration` holds this string; a summary stores it in `configuration_hash` and stores the full profile in `configuration`.

### Trace record

A timestamped observation stored as `TraceEvent` and written to JSONL. Its `kind` determines the meanings of `id`, `cycle` and `value`. Recording or replaying it does not advance hardware time.

### Simulation watchdog

A deadline in simulation ticks for completing the run. Expiry reports failure to drain; it is not a host-time timeout.

### Modeled fault

A typed violation such as late admission, invalid encoding or resource conflict. It terminates the simulated run; input or construction errors can fail before simulation starts.

### Trap

An ISA-level exceptional instruction effect. ECALL and EBREAK produce distinct modeled traps; privileged trap-handler execution is unsupported.

### Deterministic execution

Execution reproducible for fixed inputs, configuration and the same implementation. Variable preparation latency does not itself imply randomness. Equal seeds across different numerical libraries do not promise equal samples.

## Related architectures

### QuMA-style TCU

Timing and event queues separate variable command preparation time from planned output.

### Distributed-HISQ

An architectural reference for RISC-V control extensions and codeword operations. Binary compatibility is not claimed.

### TQEC integration

A future adapter for compiling TQEC workloads and consuming their results.
