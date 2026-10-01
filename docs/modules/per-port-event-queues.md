# Per-Port Event Queues

The TCU keeps one event FIFO for each physical output port. Events carry the
label of their timing point, so one label can select simultaneous actions from
several ports.

## Connections

- **Input:** resolved `OperationEvent` values inserted during atomic timing point enqueue.
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

When a timing point becomes due, the TCU gathers each queue's leading events
with that label. It compares their IDs with the timing point's manifest. A port
absent from the manifest remains idle, and entries for later labels stay queued.

After condition evaluation and device preflight succeed, all events for the
triggered label leave their queues together. An event with a false condition is
consumed with a cancellation record rather than sent to the device.

`event_capacity` limits retained entries per port. `firing_width` limits how many
entries one port may contribute to a timing point. These are different bounds: spare
storage does not allow a timing point to exceed its event triggering width.

Matching adds no extra modeled stage. Enqueue checks available space using
the queues as they existed at the start of the TCU edge.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `events_` | `vector<deque<OperationEvent>>` | One FIFO per physical output port. |
| `OperationEvent::label and id` | timing point and member identities | Matches queued members to the timing-head manifest. |
| `Profile::event_capacity and firing_width` | bounds | Storage and same-point output limits per port. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

A missing or extra member, duplicate ID, stale epoch or orphan event preceding
the timing head faults the complete timing point before launch. Reset empties every
port queue. No partial dequeue is committed after a failed preflight.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.atomic`.

The tests check simultaneous output on several ports and capacity before event triggering.
A failed preflight must leave every event in the timing point queued.
