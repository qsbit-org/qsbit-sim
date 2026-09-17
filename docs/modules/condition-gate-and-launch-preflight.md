# Condition Gate and Launch Preflight

Before a due group launches, the TCU evaluates its conditions and asks
`DeviceRuntime` to check the surviving actions. This prevents part of a group
from starting before a conflict in another member is discovered.

## Connections

- **Input:** a complete due event group, previously committed `FastHistory`, and
  existing device reservations.
- **Output:** a `LaunchBatch`, cancellation records, or a typed fault.
- **Caller:** the TCU edge transition, before it removes the due group from its queues.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Due group and committed measurement history"];
  owner [label="TcuCycleModel / DeviceRuntime"];
  state [label="FastHistory\nLaunchBatch\nResourceCalendar"];
  output [label="Preflighted LaunchBatch / cancellation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Conditions and whole-group checks

Every condition refers to an exact measurement token and expected bit.
The TCU reads the history committed before this edge. A false condition removes
that event from the candidate launch and produces `ConditionCancelled`.
An unavailable token is an error, not a false result.

`DeviceRuntime::preflight()` checks all remaining actions against each other
and existing reservations. It validates resources, backend support, readout
pairing and unsupported sampling collisions before the TCU commits queue removal.
Physical intervals include output delays, so future conflicts are checked even
when an action has not started yet.

The transition also validates candidate admission and incoming fast results.
Only after all these checks succeed does it commit firing, admission and new
history. A result arriving on this edge cannot affect its own firing decision.
Preflight cannot postpone a conflicting action to another cycle.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `FastHistory` | TCU-owned history | Exact-token results committed on earlier edges. |
| `LaunchBatch` | temporary candidate | Due label and the actions whose conditions are true. |
| `ResourceCalendar` | device-owned reservations | Checks same-batch and future interval conflicts. |

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
