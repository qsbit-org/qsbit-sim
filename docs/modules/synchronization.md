# Synchronization

The synchronization unit implements neighbor synchronization using BISP, the
booking-based synchronization protocol from Distributed-HISQ. A `sync target`
instruction adds a synchronization event at the current time point. The target
must name a directly connected controller.

## Booking and completion

When the TCU triggers the event, SyncU sends a signal to the target and starts
a countdown equal to the configured outgoing link delay. The timer continues
while this countdown runs.

At the countdown deadline, the timer pauses if the peer's signal has not
arrived. Synchronization releases its pause when the signal arrives; an empty
queue after `wait 0` can keep the TCU paused. A signal received before the
deadline is retained until the countdown completes. Each signal satisfies one
synchronization request.

For booking ticks B0 and B1 and directional delays L0 and L1, both controllers
satisfy their conditions at `max(B0 + L0, B1 + L1)`. Schedule each synchronized
operation at its local booking cycle plus its outgoing delay in cycles.
Physical output delays apply after event triggering.

The protocol follows [Distributed-HISQ, Section 4.1](https://arxiv.org/html/2509.04798v1#S4.SS1).

## Connections and state

`Core` owns a `SyncUnit`. `Simulator` owns the shared `SyncNetwork`.
The network stores signals with fixed arrival ticks in bounded directional
queues. SyncU retains one active target, countdown deadline and received flag.
It updates these before the TCU transition on each TCU edge. Subsequent sync
events occupy the bounded TCU synchronization queue until their time points trigger.
Starting another synchronization before the active one completes raises
`UnsupportedSynchronization`.

Pausing the TCU does not pause CPU execution, physical time, active pulses,
acquisition or backend evolution. Queues can still accept future time points
and measurement results while the timer is paused.

## Reset and errors

Session reset clears requests, signal queues and pause state across all cores.
An unconnected target raises `UnsupportedSynchronization`. A full TCU synchronization
queue delays admission; exceeding a signal queue's capacity raises `Capacity`.
A missing peer request prevents
completion and eventually raises `Watchdog`.

Regional synchronization through routers is unsupported.

## Implementation and tests

Source: [sync.cpp](../../src/sync.cpp), [sync.hpp](../../include/qsbit/sync.hpp),
[core.cpp](../../src/core.cpp) and [tcu.cpp](../../src/tcu.cpp).

**CTest:** `sync.neighbor`, `systemc.distributed`.

The tests cover early and late peer signals, directional delays, repeated
requests, bounded queues, reset, unconnected targets and unmatched requests.
Integration checks preserve output timestamps when process order is reversed.

See [configuration](../distributed-simulation.md) and the
[dual-board example](../../examples/distributed-hisq/README.md).
