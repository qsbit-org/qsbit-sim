# Conditional execution checks

Before triggering events at a due time point, the TCU evaluates their conditions
and asks `ControlElectronics` to validate the selected events. All checks finish
before any event starts.

## Connections

- **Input:** all events at the due time point, previously committed `ConditionalResults`, and
  existing device reservations.
- **Output:** a `TriggeredEvents`, cancellation records, or a typed fault.
- **Caller:** the TCU edge transition, before it removes the due time point from its queues.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Due time point and stored measurement results"];
  owner [label="TcuCycleModel and ControlElectronics"];
  state [label="ConditionalResults\nTriggeredEvents\nResourceReservations"];
  output [label="Validated TriggeredEvents and cancellation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Conditions and event validation

Each condition contains a measurement reference and an expected bit.
The TCU tests it against results committed before the current edge.
A false condition removes the event from output and emits
`ConditionCancelled`. A missing result raises `InvalidToken`.

`ControlElectronics::preflight()` checks the selected events for resource
conflicts, backend support, acquisition and arm pairing, and a gate
starting on the same target and tick as a measurement sample. The checks
include future intervals introduced by output delays.

The TCU also validates the enqueue request and incoming results before
committing any changes. A failure cancels the whole transition; events
are not moved to a later cycle.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `ConditionalResults` | TCU-owned history | Results committed on earlier edges, indexed by measurement reference. |
| `TriggeredEvents` | temporary candidate | Due label and the events whose conditions are true. |
| `ResourceReservations` | device-owned reservations | Checks same-batch and future interval conflicts. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

Unavailable history, conflicting resources or invalid events raise a typed
fault and suppress the whole TCU transition's output. Conditional acquisition
is unsupported.

This check has no independent persistent state. Reset clears the TCU history
and device reservations that it reads.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `control.fast`, `protocol.calendar`, `protocol.sample_collision`.

The tests distinguish an unavailable result from a false condition. They also
check that resource conflicts and unsupported sampling collisions fail before
any part of the device batch changes state.
