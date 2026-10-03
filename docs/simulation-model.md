# Simulation time and execution

The CPU prepares events while the TCU controls their output timing. Both
run on clock edges scheduled by SystemC. Device events can also occur
between clock edges.

## Time, clocks and precision

One tick is 1 ns of simulation time. A clock with period 20 ns and phase
3 ns has rising edges at 3, 23, 43 ns and so on. The CPU and memory share
a clock; the TCU has its own. The TCU start tick selects logical cycle zero.

SystemC runs processes scheduled at the current tick, then advances to
the next scheduled event. Extra scheduling rounds at the same tick,
called delta cycles, consume no simulation time.

Numerical calculations consume host time without advancing simulation time.
For example, a backend call that takes milliseconds can calculate a gate
assigned to a single simulation tick.

## Modules, objects and processes

`Simulator` registers five SystemC methods:

| Process | Trigger | Work |
| --- | --- | --- |
| CPU edge | CPU rising edge | Receive replies and results, then step the CPU. |
| Memory edge | CPU rising edge | Service instruction and data requests. |
| TCU edge | TCU rising edge | Trigger due events, enqueue requests and receive results. |
| Timed wakeup | Next device event, reset or watchdog deadline | Apply reset if due and request device processing. |
| Device barrier | Notification in the next delta cycle | Wait for clocked methods to finish, process device events and check completion. |

The CPU, memory, timing control, TCU and device models are ordinary C++ objects
called by these methods. Their state persists between calls. A blocked
instruction retains its operands until a later CPU edge retries it.

The methods use `dont_initialize()`; they first run when their triggering
event occurs. A clock edge at time zero can still invoke them.

## Work at one simulation tick

When CPU and TCU edges coincide, SystemC can invoke their methods in either
order. Mailbox arrival times prevent a message published at that tick from
being consumed on the same edge.

Each clocked method records when it finishes and requests device processing
in a later delta cycle. The device barrier processes the tick only after all
clocked methods due at that tick have finished. Events triggered with zero
output delay therefore join other device events at the same tick.

The models also specify the order of changes within an edge. CPU operand
capture can read a value retired earlier on that edge. TCU conditions use
results committed before the edge. See
[TCU edge order](module-architecture.md#communication-latency-and-tcu-edge-order).

## Messages and direct calls

A mailbox retains a payload, epoch, publication tick and arrival tick.
A message arrives on the first receiver edge strictly after publication,
plus any additional configured receiver periods.

For TCU edges at 20, 40 and 60 ns, a message published at 20 ns arrives at
40 ns with one-edge latency, or 60 ns with two-edge latency. A full
destination queue can delay consumption further; the event deadline stays
unchanged.

Direct calls such as codeword lookup add no communication delay.
SystemC events wake methods; mailboxes retain the messages. The timed
wakeup cancels its previous notification whenever it selects a new deadline.

## From a command to device work

A port and codeword select event specifications from the profile. Each
specification names an output port, delay and duration. Targets and resources
are specified directly or supplied by a paired gate definition.
The instruction's port can differ from the output port.

`TimingControl` collects events for the current time point. It submits the
time point and all its events together, keeping the request unchanged
until the TCU acknowledges insertion.

An event starts at `fire_tick + delay`. Different output delays can separate
events triggered together; events from different time points can start
at the same tick. `ControlElectronics` processes all device events at a tick
together against the shared quantum state.

An ideal gate changes state at its start. A pulse contributes to evolution
throughout its interval. Both occupy their configured ports and resources
until the interval ends. Conflicting intervals cause a fault rather than
rescheduling an operation.

## Measurement and completion

At acquisition end, the backend samples the measurement. After the
discriminator delay, the result travels to the CPU and, when enabled, the
TCU's execution flags.

A result delivered before the CPU step can complete FMR on that edge.
The TCU commits new results after evaluating conditions, so they first
become usable on its next edge.

The exit ECALL halts the CPU after enqueueing any pending events. The simulation
continues until the TCU receives closure and all queues, device events,
memory requests and enabled result deliveries have completed.
A fault or watchdog expiry stops the run with failure.

Session reset starts a new epoch and resets the controller and backend,
while preserving memory and the profile. Simulation time continues.
The [reset rules](module-architecture.md#session-reset) specify the new TCU
start and stale-result handling.
