# Timing Queue

The timing queue records when admitted groups should fire. Each entry contains
an interval, label and list of expected event IDs. The TCU uses it alongside the
per-port event queues to release a complete group at its planned cycle.

## Connections

- **Input:** a `TimingPoint` accepted with its event group by TCU admission.
- **Output:** the next label, due cycle and manifest for the TCU firing decision;
  queue occupancy for admission checks.
- **Owner:** `TcuCycleModel::timing_`, updated during the TCU edge transition.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Atomic TimingPoint admission"];
  owner [label="TcuCycleModel::timing_"];
  state [label="Point::point\nPoint::due\nlast_due_"];
  output [label="Head TimingPoint and cumulative due cycle"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Keeping time across queue gaps

Admission adds the new interval to `last_due_`, then stores both the original
timing point and its cumulative due cycle. The first interval is measured from
logical cycle zero.

For example, intervals 4 and 3 describe points due at cycles 4 and 7. If the
queue empties after cycle 4, a later interval of 3 still describes cycle 7.
It succeeds only if admitted before that deadline; arrival time never replaces
the planned cycle.

At a due edge, the TCU checks the head's manifest, conditions and device
reservations. Once the transition is valid, it removes the timing head and its
port events together. Only an entry present before the edge can fire.

An empty manifest is a valid wait-only point. An empty FIFO means that no point
is currently queued; the TCU timer continues running.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Point::point` | `TimingPoint` | Epoch, label, interval and exact event-ID manifest. |
| `Point::due` | logical TCU cycle | Cumulative due cycle calculated during admission. |
| `last_due_` | logical TCU cycle | Last admitted due cycle, retained even when the FIFO empties. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

Admission on or after the due tick raises `LateAdmission`. Missing, extra
or stale manifested events raise `ManifestMismatch` before the group launches.
Reset clears the queue, cumulative due cycle and label sequence.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.empty`.

The tests check queue capacity before firing and cumulative due times,
including deadlines retained while the queue is empty.
