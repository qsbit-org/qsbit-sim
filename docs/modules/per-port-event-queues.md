# Per-port event queues

The TCU keeps one event FIFO for each physical output port. Events carry the
label of their time point, so one label can select simultaneous events from
several ports.

## Connections

- **Input:** resolved `OperationEvent` values inserted during atomic time point enqueue.
- **Output:** the matching events for a due label and occupancy for enqueue checks.
- **Owner:** `TcuCycleModel::events_`; these queues share the TCU edge with the timer
  and timing queue.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TCU atomic event enqueue"];
  owner [label="TcuCycleModel::events_"];
  state [label="events_\nOperationEvent::label and id\nProfile::event_capacity and firing_width"];
  output [label="Matching-label event members"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Matching a label

When a time point is due, the TCU collects the leading events with that
label from each port queue. Their IDs must match the time point's
`manifest` exactly. Other ports remain idle; later labels stay queued.

After condition evaluation and device validation, all events for the label
leave their queues together. A false condition produces
`ConditionCancelled` instead of sending that event to the device.

`event_capacity` limits queued entries per port. `firing_width` limits
events per port at one time point. Enqueue checks both limits using
occupancy at the start of the edge.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `events_` | `vector<deque<OperationEvent>>` | One FIFO per physical output port. |
| `OperationEvent::label and id` | time point and member identities | Matches queued members to the timing-head manifest. |
| `Profile::event_capacity and firing_width` | bounds | Storage and same-point output limits per port. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

A missing or extra member, duplicate ID, stale epoch or orphan event preceding
the timing head faults the complete time point before output. Reset empties every
port queue. No partial dequeue is committed after failed validation.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.atomic`.

The tests check simultaneous output on several ports and capacity before triggering.
Failed validation must leave every event in the time point queued.
