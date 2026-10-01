# Queue enqueue and communication latency

An enqueue request travels from CPU-side timing control to the TCU through a mailbox.
The TCU accepts its timing point and all port events together. Temporary queue
pressure leaves the same timing point waiting in the mailbox.

## Connections

- **Input:** the timing control's submitted `TimingEvents` through `ControlLinks::groups`.
- **Output:** entries in the TCU timing and event queues, followed by an `EnqueueReply`
  through the return mailbox.
- **Scheduling:** the TCU checks an arrived request on a TCU rising edge;
  the CPU receives its reply on a later CPU edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="enqueue request mailbox"];
  owner [label="ControlLinks and TcuCycleModel"];
  state [label="groups, replies, closure\nEnvelope\nTcuCycleModel::last_label_"];
  output [label="Queue insertion and EnqueueReply"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Crossing and acceptance

Publishing a message records its epoch and arrival tick. Arrival time
starts at the first receiver edge strictly after publication, followed by any
additional configured receiver periods. With TCU edges at 20 and 40 ns and a
one-edge crossing, a timing point published at 20 ns is first eligible at 40 ns.
Arrival time permits inspection; enqueue can occur later if storage is full.
The [mailbox lifecycle](../glossary.md#mailbox) distinguishes publication,
arrival timing and consumption.

The TCU checks the timing point's manifest, profile fingerprint, ordered label, deadline
and queue requirements. If current occupancy leaves enough room, it inserts the
timing point and every event in one transition. `Simulator` then removes the
mailbox request and publishes its label in `EnqueueReply`.

Capacity is measured before any entries trigger on that edge. Space freed by event triggering
becomes usable on the next TCU edge. A full queue leaves the candidate in place;
its original deadline continues to approach while it waits.

Enqueue must occur strictly before the timing point's due tick. Enqueue itself
does not launch the timing point, and a newly enqueued timing point cannot trigger on the same
edge. See [crossing and edge order](../module-architecture.md#communication-latency-and-tcu-edge-order)
for the complete transition.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `groups, replies, closure` | Mailboxes of `TimingEvents`, `EnqueueReply` and `EndOfStream` | One-slot communication paths between timing control and TCU; `groups` is a compatibility member name. |
| `Envelope` | `published, eligible, epoch, value` | Retains the payload until its receiver can consume it. |
| `TcuCycleModel::last_label_` | last enqueued label | Enforces ordered, nonduplicate submission. |
| `timing_, events_` | TCU queues | Enqueue checks occupancy at the start of the edge before inserting the whole timing point. |

[C++ API](../api.md#producerhpp).

## Reset and errors

Malformed timing points, duplicate or stale identities, impossible capacity and
late arrival raise typed faults. The TCU validates the whole transition before
committing its queue changes. A fault in candidate enqueue also suppresses
any launch planned by that TCU transition.

Session reset clears the crossing mailboxes and TCU queues. Epoch checks prevent
old work from entering the new session.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [mailbox.hpp](../../include/qsbit/mailbox.hpp).

**CTest:** `core.mailbox`, `control.admission`, `control.atomic`.

The tests check delivery on a strictly later receiver edge and enqueue using
capacity from the start of the edge. Failed preflight must leave queues unchanged.
