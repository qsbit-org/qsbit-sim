# Per-Port Event Queues

The TCU keeps one event FIFO for each physical output port. Events carry the
label of their timing point, so one label can select simultaneous actions from
several ports.

## Connections

- **Input:** resolved `ReservedEvent` values inserted during atomic group admission.
- **Output:** the matching events for a due label and occupancy for admission checks.
- **Owner:** `TcuCycleModel::events_`; these queues share the TCU edge with the timer
  and timing queue.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TCU atomic event admission"];
  owner [label="TcuCycleModel::events_"];
  state [label="events_\nReservedEvent::label / id\nProfile::event_capacity / firing_width"];
  output [label="Matching-label event members"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Matching a label

When a timing point becomes due, the TCU gathers each queue's leading events
with that label. It compares their IDs with the timing point's manifest. A port
absent from the manifest remains idle, and entries for later labels stay queued.

After condition evaluation and device preflight succeed, all events for the
fired label leave their queues together. An event with a false condition is
consumed with a cancellation record rather than sent to the device.

`event_capacity` limits retained entries per port. `firing_width` limits how many
entries one port may contribute to a group. These are different bounds: spare
storage does not allow a group to exceed its firing width.

Matching adds no extra modeled stage. Admission checks available space using
the queues as they existed at the start of the TCU edge.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `events_` | `vector<deque<ReservedEvent>>` | One FIFO per physical output port. |
| `ReservedEvent::label / id` | group/member identities | Matches queued members to the timing-head manifest. |
| `Profile::event_capacity / firing_width` | bounds | Storage and same-point output limits per port. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

A missing or extra member, duplicate ID, stale epoch or orphan event preceding
the timing head faults the complete group before launch. Reset empties every
port queue. No partial dequeue is committed after a failed preflight.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.atomic`.

The tests check simultaneous output on several ports and capacity before firing.
A failed preflight must leave every event in the group queued.
