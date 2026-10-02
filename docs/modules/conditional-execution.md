# Conditional execution checks

Before triggering events at a due time point, the TCU evaluates their conditions
and asks `ControlElectronics` to validate the selected events. All checks finish
before any event starts.

## Connections

- **Input:** all events at the due time point, previously committed `ExecutionFlags`, and
  existing device reservations.
- **Output:** a `TriggeredEvents`, cancellation records, or a typed fault.
- **Caller:** the TCU edge transition, before it removes the due time point from its queues.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Due time point and execution flags"];
  owner [label="TcuCycleModel and ControlElectronics"];
  state [label="ExecutionFlags\nTriggeredEvents\nResourceReservations"];
  output [label="Validated TriggeredEvents and cancellation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Conditions and event validation

Each event selects an execution flag in its codeword mapping. The TCU checks
the target qubit's flag before committing results received on the current edge.
A zero flag suppresses the event and emits `ConditionCancelled`.

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
| `ExecutionFlags` | TCU-owned registers | Per-qubit flags updated by earlier measurement results. |
| `TriggeredEvents` | temporary candidate | Due label and the events whose conditions are true. |
| `ResourceReservations` | device-owned reservations | Checks same-batch and future interval conflicts. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

Conflicting resources or invalid events raise a typed fault and suppress the
transition's output. Conditional acquisition is unsupported.

Reset clears the execution flags and device reservations.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `control.fast`, `protocol.resources`, `protocol.sample_collision`.

Tests check trigger-time flag selection and same-edge result arrival.
Resource conflicts and sampling collisions fail before device state changes.
