# Timing queue

The timing queue records when enqueued time points should trigger. Each entry contains
an interval, label and list of expected event IDs. The TCU uses it alongside the
per-port event queues to release all associated events at their planned cycle.

## Connections

- **Input:** a `TimingPoint` enqueued together with its associated events.
- **Output:** the next label, due cycle and manifest for the TCU triggering decision;
  queue occupancy for enqueue checks.
- **Owner:** `TcuCycleModel::timing_`, updated during the TCU edge transition.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Atomic TimingPoint enqueue"];
  owner [label="TcuCycleModel::timing_"];
  state [label="Point::point\nPoint::due\nlast_due_"];
  output [label="Head TimingPoint and cumulative due cycle"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Keeping time across queue gaps

Each enqueue adds its interval to `last_due_` and stores the resulting
due cycle. The first interval is measured from logical cycle zero.

Intervals 4 and 3 therefore specify cycles 4 and 7. Even if the queue
empties after cycle 4, the second interval still refers to cycle 7 and
must be enqueued before that cycle.

At the due edge, the TCU checks the entry's event IDs, conditions and
resource requirements. If the transition passes validation, it removes
the time point and its port events together.

A time point with no events represents a wait. An empty queue leaves the
timer running.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Point::point` | `TimingPoint` | Epoch, label, interval and exact event-ID manifest. |
| `Point::due` | logical TCU cycle | Cumulative due cycle calculated during enqueue. |
| `last_due_` | logical TCU cycle | Last enqueued due cycle, retained even when the FIFO empties. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

Enqueue on or after the due tick raises `LateAdmission`. Missing, extra
or stale manifested events raise `ManifestMismatch` before triggering.
Reset clears the queue, cumulative due cycle and label sequence.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.empty`.

The tests check queue capacity before triggering and cumulative due times,
including deadlines retained while the queue is empty.
