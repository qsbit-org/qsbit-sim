# Conditional execution checks

Before triggering events at a due time point, the TCU evaluates their conditions
and asks `ControlElectronics` to validate the selected events. All checks finish
before any event starts.

## Connections

- **Input:** all events at the due time point, previously committed `ConditionalResults`, and
  existing device reservations.
- **Output:** a `TriggeredEvents`, cancellation records, or a typed fault.
- **Caller:** the TCU edge transition, before it removes the due timing point from its queues.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Due timing point and committed measurement history"];
  owner [label="TcuCycleModel and ControlElectronics"];
  state [label="ConditionalResults\nTriggeredEvents\nResourceReservations"];
  output [label="Preflighted TriggeredEvents and cancellation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Conditions and event validation

Every condition refers to an exact measurement token and expected bit.
The TCU reads the history committed before this edge. A false condition removes
that event from the candidate launch and produces `ConditionCancelled`.
An unavailable token is an error, not a false result.

`ControlElectronics::preflight()` checks all remaining actions against each other
and existing reservations. It validates resources, backend support, readout
pairing and unsupported sampling collisions before the TCU commits queue removal.
Physical intervals include output delays, so future conflicts are checked even
when an action has not started yet.

The transition also validates candidate enqueue and incoming fast results.
Only after all these checks succeed does it commit event triggering, enqueue and new
history. A result arriving on this edge cannot affect its own event triggering decision.
Preflight cannot postpone a conflicting action to another cycle.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `ConditionalResults` | TCU-owned history | Exact-token results committed on earlier edges. |
| `TriggeredEvents` | temporary candidate | Due label and the actions whose conditions are true. |
| `ResourceReservations` | device-owned reservations | Checks same-batch and future interval conflicts. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

Unavailable history, conflicting resources or invalid actions raise a typed
fault and suppress the whole TCU transition's launch. Conditional acquisition
is unsupported.

This check has no independent persistent state. Reset clears the TCU history
and device reservations that it reads.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `control.fast`, `protocol.calendar`, `protocol.sample_collision`.

The tests distinguish an unavailable result from a false predicate. They also
check that resource conflicts and unsupported sampling collisions fail before
any part of the device batch changes state.
