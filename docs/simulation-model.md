# Simulation time and execution

qsbit-sim models a controller whose CPU prepares commands before the TCU releases
them. A CPU stall can delay preparation while the TCU clock keeps running.
The [glossary](glossary.md) defines the terms used below.

## Time, clocks and precision

SystemC maintains **simulation time** and calls registered processes when events
occur. A clock edge is one such event. A device action can also schedule a
boundary between clock edges. When there is no work left at the current time,
the kernel advances to the next scheduled time.

A tick is 1 ns. The CPU and memory run on CPU rising edges; the TCU runs on its
own rising edges. With period 20 ns and phase 3 ns, a clock has edges at 3, 23,
43 ns and so on. Phase locates clock edges relative to global time zero. TCU
start chooses which TCU edge is logical cycle zero.

The model is cycle-level because its pipeline, queues and transfers have defined
clock-edge behavior. Event-driven scheduling is how those transitions are run.
The 1 ns resolution does not imply a physical model accurate to 1 ns or a CPU
matching a particular manufactured processor.

Host time measures how long the executable takes on your computer. A numerical
backend may take milliseconds to calculate a state change assigned to one
simulation tick. That host computation does not delay the modeled controller.

## Modules, objects and processes

One `Simulator` SystemC module connects ordinary C++ model objects. It registers
five `SC_METHOD` processes:

| Process | Trigger | Model work |
| --- | --- | --- |
| CPU edge | CPU rising edge | Receive eligible results and replies, then step the CPU and timeline producer. |
| Memory edge | CPU rising edge | Service instruction and data transactions. |
| TCU edge | TCU rising edge | Check firing, admission and incoming fast results. |
| Timed wakeup | Earliest scheduled device, reset or watchdog time | Apply reset if due and request device processing. |
| Device barrier | Next-delta notification | Check edge completion, process physical work and test for drain. |

A logical module in the architecture diagram names a responsibility. A C++ owner
stores and changes state. A SystemC process schedules an invocation. These are
separate boundaries: several responsibilities can share one object, and several
objects can be called by one process. An owner boundary adds no delay by itself.

During elaboration, the application constructs models, registers processes and
connects triggers. Static sensitivity identifies the events that wake each
process. The methods use `dont_initialize()` to suppress automatic initialization
calls; a time-zero clock event can still trigger them.

## Work at one simulation tick

A method runs until it returns. It cannot call `wait()` to suspend. A blocked
CPU instruction keeps its state and operands; the next CPU edge retries it.
Other clocks and device boundaries continue to run during that modeled stall.

When CPU and TCU edges coincide, the kernel can run their ready methods in
either order. Messages have explicit eligibility timestamps, so a newly
published message cannot be consumed on that same edge. Each clocked method
also marks that it has completed its work and requests the device barrier in
a later delta cycle at the same simulation time.

The barrier processes physical work only after all clocked methods due at that
tick have finished. A TCU action with zero output delay can therefore join the
same physical boundary batch. These extra scheduling rounds consume no modeled
nanoseconds or clock cycles.

SystemC signal updates and model commits have different rules. A primitive
channel such as `sc_signal` applies a requested write in the kernel update phase.
The ordinary C++ models apply changes according to their own transition order:
CPU operand capture can read a value retired earlier on the same edge, while
TCU conditions read history committed before the current edge. The
[TCU transition](module-architecture.md#crossing-and-tcu-edge-order) specifies
which changes commit together after validation.

## Messages and direct calls

A mailbox stores a message and its epoch, publication tick and eligibility tick.
Eligibility is the earliest allowed reception, not a promise of acceptance.
A full destination queue can leave an eligible group pending until a later edge;
its original firing deadline still applies.

For a one-edge crossing to a TCU clock with edges at 20 and 40 ns, publication
at 20 ns first becomes eligible at 40 ns. With two-edge latency it becomes
eligible at 60 ns. The latency is counted in receiver edges, not CPU cycles.

Direct calls, including action lookup and device preflight, carry values without
an added communication stage. Only connections declared as timed mailboxes use
the crossing rule. A SystemC event wakes a method; the message remains in its
mailbox or owning model. The timed wakeup cancels its previous notification
before selecting the next deadline because an `sc_event` is not a queue of
future messages.

## From a command to device work

A source control port and codeword select one or more action descriptors.
Each descriptor names a physical output port, ordered qubit targets, resources,
output delay and duration. The source port need not equal a target qubit or the
physical output port.

The timeline producer gives the actions control-event identities and collects
them into an open group at its cursor. Sealing fixes that group for submission.
Admission puts its timing point and all port events into the TCU queues together.
Firing releases the condition-selected events as one launch batch.

Each action starts at `fire_tick + delay`. A launch with delays 0 and 10 ns
produces starts at different physical boundaries. Conversely, actions from
different launches can meet at the same boundary. `DeviceRuntime` processes that
complete physical boundary batch against shared quantum state.

An ideal gate changes state at its start and reserves resources until its end.
A pulse drive instead contributes to evolution throughout its interval. A
resource calendar detects overlaps, including conflicts between different ports
that share a resource. Available queue space does not imply available physical
resources.

## Measurement and completion

Acquisition ends with a backend measurement sample. The discriminator contributes
a modeled delay; qsbit-sim does not classify a raw readout waveform. The bit then
travels independently to the CPU and, when enabled, to TCU history.

A CPU result arriving before the CPU step can complete QREAD on that edge.
`FastResultVisible` records a history commit at the end of a TCU transition;
conditions first use the new result on a later TCU edge. The fast path bypasses
a CPU branch but need not deliver a bit sooner than the CPU path.

Bell returns measurement results without making a conditional decision. The
feedback example consumes a result, branches and schedules another gate.
Both use result delivery, but only the latter uses CPU conditional feedback.

QEND closes production and halts the CPU after its required flush. Success still
waits for TCU closure, queued work, physical actions, memory and enabled result
deliveries to drain. Unread Visible CPU result slots can remain. A watchdog or
fault stops the run as a failure instead.

Session reset starts a new epoch without rewinding simulation time. It preserves
memory and the simulation profile. The new TCU origin is the first TCU edge at
or after `reset_tick + profile.start`; the CPU edge index continues on the global
clock. Measurement-slot generations survive reset to keep old handles invalid.
