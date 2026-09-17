# Output Channels and Resource Calendar

`DeviceRuntime` schedules physical actions after the TCU fires a group.
Its resource calendar reserves the complete future interval for each action,
including actions whose output delay means they have not started yet.

## Connections

- **Input:** validated TCU `LaunchBatch` values with resolved action descriptors.
- **Output:** physical starts and ends, active pulse drives, backend calls and
  measurement completions.
- **Scheduling:** the SystemC barrier calls `process()` at the next physical
  boundary after all clocked work due at that tick has finished.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Immutable TCU launch batch"];
  owner [label="DeviceRuntime"];
  state [label="calendar_\nboundaries_\nactive_"];
  output [label="Physical actions / backend calls / Completion"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Reserving and executing intervals

`accept()` converts each event to an interval:
`start = fire_tick + delay`, `end = start + duration`.
It checks the complete batch against existing reservations and then installs
all intervals. Resource occupancy is half-open, `[start, end)`, so one action
can end at the exact tick another begins.

At a physical boundary, the runtime validates the whole boundary batch. It
evolves quantum state under the previously active drives up to this tick,
samples ending acquisitions, removes ended actions, applies starting ideal
gates and activates new intervals. Ready results are published last.

Independent ports may operate together. Overlapping pulses can share a target
when their resource declarations permit it and the backend supports joint
evolution. Port iteration order must not change the resulting quantum state.

For pulses active over [10,30) and [20,40), backend evolution covers [10,20)
with the first drive, [20,30) with both, and [30,40) with the second.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `calendar_` | `ResourceCalendar` | Future half-open physical reservations and action identities. |
| `boundaries_` | `map<Tick, Boundary>` | Scheduled starts, ends and result-ready IDs. |
| `active_` | `map<Id, PhysicalAction>` | Operations currently occupying physical intervals. |
| `last_tick_, processed_tick_` | global ticks | Last evolution point and guard against repeated physical processing. |

[C++ API](../api.md#devicehpp).

## Reset and errors

All action durations must be positive. Exclusive-resource conflicts and
unsupported same-target sampling collisions fail before quantum state changes.
A backend failure stops the run; already performed numerical work is not rolled
back or retried.

Reset aborts active actions, clears future boundaries and reservations, and
initializes the backend for the new epoch. Global time continues from the
reset tick.

## Implementation and tests

Source: [device.cpp](../../src/device.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `protocol.calendar`, `protocol.sample_collision`, `protocol.reset`.

The tests check overlapping resource intervals, unsupported sampling collisions
and cancellation of active device work during reset.
