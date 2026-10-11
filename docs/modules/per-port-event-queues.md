# Per-port event queues

The TCU keeps one event FIFO for each configured local output port. Events carry the
label of their time point, so one label can select simultaneous events from
several ports.

## Connections

- **Input:** resolved `OperationEvent` values inserted during atomic time point enqueue.
- **Output:** the matching events for a due label and occupancy for enqueue checks.
- **Owner:** `TcuCycleModel::events_`; these queues share the TCU edge with the timer
  and timing queue.

## Matching a label

When a time point is due, the TCU collects the leading events with that
label from each port queue. Their IDs must match the time point's
`manifest` exactly. Other ports remain idle; later labels stay queued.

After condition evaluation and device validation, all events for the label
leave their queues together. A false condition produces
`ConditionCancelled` instead of sending that event to the device.

`event_capacity` limits queued entries per output port. `firing_width` limits
events per output port at one time point. See [enqueue checks](queue-enqueue.md).

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `events_` | `vector<deque<OperationEvent>>` | One FIFO per configured local output port. |
| `OperationEvent::label and id` | time point and member identities | Matches queued members to the timing-head manifest. |
| `TimingConfig::event_capacity and firing_width` | bounds | Storage and same-point output limits per port. |

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
