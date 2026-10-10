# Control output and resource checks

`ControlElectronics` schedules physical events after the TCU triggers a time point.
Its resource checker reserves the complete future interval for each event,
including events whose output delay means they have not started yet.

## Connections

- **Input:** validated TCU `TriggeredEvents` values with resolved event specifications.
- **Output:** physical starts and ends, active pulse drives, backend operations and
  measurement completions.
- **Scheduling:** the SystemC barrier calls `process()` at the next physical
  boundary after all clocked work due at that tick has finished.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TriggeredEvents"];
  owner [label="ControlElectronics"];
  state [label="reservations_\nboundaries_\nactive_"];
  output [label="Device events, backend operations and Completion"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Reserving and executing intervals

`accept()` calculates `start = fire_tick + delay` and
`end = start + duration` for every event. It checks all intervals against
one another and existing reservations before installing them.

Intervals use `[start, end)`, so adjacent operations can share an endpoint.
Overlaps on one output port are forbidden. Intervals on different ports
conflict when they share a resource and either use is exclusive. Except for
discriminator arms, events sharing a target may overlap only when both
are pulses or are the two configured outputs of one gate with identical intervals.

For a `GateOutput`, `resolve()` checks the source core, local port and codeword
against the gate definition and reserves its targets and resources. At the start
tick, `process()` requires both configured outputs before any backend mutation.
It commits one ideal gate and emits one `GateApplied` record. Each output retains
its own `OperationStart` and `OperationEnd` records.

At each device event tick, the model validates the scheduled work, commits
the preceding evolution interval, samples ending acquisitions, removes ended events,
commits starting gates and activates new intervals. It publishes ready results last.
`BackendExecution` collects the committed operations and executes them when a
measurement needs an outcome, on inspection, at the batch limit or before completion.

Permitted overlapping pulses evolve together. For drives active over
[10,30) and [20,40), evolution covers [10,20) with the first drive,
[20,30) with both, and [30,40) with the second.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `reservations_` | `ResourceReservations` | Future half-open physical reservations and event identities. |
| `boundaries_` | `map<Tick, Boundary>` | Scheduled starts, ends and result-ready IDs. |
| `active_` | `map<Id, ScheduledEvent>` | Operations currently occupying physical intervals. |
| `last_tick_, processed_tick_` | global ticks | Last evolution point and guard against repeated physical processing. |

[C++ API](../api.md#devicehpp).

## Reset and errors

All event durations must be positive. Exclusive-resource conflicts and
unsupported same-target sampling collisions fail before quantum state changes.
A backend failure stops the run; already performed numerical work is not rolled
back or retried.

Reset aborts active and future events, discards pending backend operations, clears reservations, and
initializes the backend for the new epoch. Global time continues from the
reset tick.

## Implementation and tests

Source: [device.cpp](../../src/device.cpp), [device.hpp](../../include/qsbit/device.hpp)
and [gates.cpp](../../src/device/gates.cpp).

**CTest:** `protocol.resources`, `protocol.sample_collision`, `protocol.reset`, `device.two_qubit`.

The tests check overlapping resource intervals, unsupported sampling collisions
and cancellation of active device work during reset. Paired-gate tests check
single application, delay compensation, missing inputs, conflicts and reset.
