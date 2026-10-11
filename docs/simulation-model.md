# Simulation time and execution

The CPU prepares control events, the TCU triggers them at scheduled time points,
and SystemC advances the simulation through clock edges and device events.

## Time, clocks and precision

One tick is 1 ns of simulation time. A clock with period 20 ns and phase
3 ns has rising edges at 3, 23, 43 ns and so on. Each core's CPU and memory
share a clock. All cores use the same TCU clock, while CPU clock settings may
differ between cores. The TCU start tick selects logical cycle zero.

SystemC processes events in timestamp order. Delta cycles allow further
processing at the same timestamp without advancing simulation time.
Backend computation consumes host time but does not advance simulation time.

## Processes and state

`Simulator` registers five SystemC methods:

| Process | Trigger | Work |
| --- | --- | --- |
| CPU edge | CPU rising edge | Receive replies and results, then step the CPU. |
| Memory edge | CPU rising edge | Service instruction and data requests. |
| TCU edge | TCU rising edge | Trigger due events, enqueue requests and receive results. |
| Timed wakeup | Next device or decoder event, reset or watchdog deadline | Apply reset if due and request device processing. |
| Device barrier | Notification in the next delta cycle | Process decoder and device events after clocked methods finish, then check completion. |

The methods call C++ models whose state persists between invocations.
[Platform and clocks](modules/platform-and-clocks.md) describes process registration
and wakeup scheduling.

## Work at one simulation tick

When CPU and TCU edges coincide, SystemC can invoke their methods in either
order. Messages become readable only on receiver edges strictly after
publication, so the order cannot change which messages a core consumes.

The device barrier waits for all clocked methods due at the tick to finish.
It then processes their zero-delay outputs together with other device events
at that tick. The [timing reference](module-architecture.md) specifies mailbox
latency, TCU edge order and physical event ordering.

SystemC events wake methods; bounded mailboxes retain messages until consumption.
Direct calls such as codeword lookup add no communication delay.

## Device work and backend execution

A source port and codeword select event specifications from the profile.
Each specification names an output port, delay and duration. An event starts at
`trigger_tick + delay`, so events triggered together can start at different ticks.

An ideal gate applies at the start of its interval. A pulse contributes to
evolution throughout the interval. Both occupy their configured output ports
and resources until the interval ends. Conflicting intervals cause a fault.

`ControlElectronics` commits operations to `BackendExecution`, which can defer
numerical execution across device events. A measurement executes pending backend
work and returns the sampled bit. See [backend execution](backends.md#backend-execution).

## Completion and reset

The exit ECALL halts its CPU after enqueueing pending events. After all CPUs halt,
the simulation continues until pending controller, device, decoder and
synchronization work has completed. A fault or watchdog expiry terminates the
run with failure. The [completion conditions](module-architecture.md#closure-and-drain)
define which work must drain.

Session reset starts a new epoch and resets all cores, the synchronization network,
the decoder system, the device and the backend. Memory and profiles are preserved;
simulation time continues. See [reset timing](module-architecture.md#session-reset).
