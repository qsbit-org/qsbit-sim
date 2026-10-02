# Queue insertion and communication latency

An enqueue request travels from CPU-side timing control to the TCU through a mailbox.
The TCU accepts its time point and all port events together. A full queue leaves the request pending in the mailbox.

## Connections

- **Input:** the timing control's submitted `TimingEvents` through `ControlLinks::timing_events`.
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
  state [label="timing_events, replies, closure\nEnvelope\nTcuCycleModel::last_label_"];
  output [label="Queue insertion and EnqueueReply"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Crossing and acceptance

A message arrives at the first receiver edge strictly after publication,
plus any additional configured receiver periods. For TCU edges at 20 and
40 ns and one-edge latency, a request published at 20 ns arrives at 40 ns.

The TCU checks the request's event IDs, profile fingerprint, label order,
deadline and required capacity. When space is available, it inserts the
time point and all its events together. `Simulator` removes the request
from the mailbox and sends an `EnqueueReply` with its label.

Capacity is checked before removing events triggered on that edge.
Space freed on the edge becomes available on the next TCU edge. A request
waiting for space retains its original deadline.

Insertion must finish strictly before the due tick. A newly inserted time
point cannot trigger on the same edge. See
[TCU edge order](../module-architecture.md#communication-latency-and-tcu-edge-order).

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `timing_events, replies, closure` | Mailboxes of `TimingEvents`, `EnqueueReply` and `EndOfStream` | One-slot communication paths between timing control and TCU. |
| `Envelope` | `published, eligible, epoch, value` | Retains the payload until its receiver can consume it. |
| `TcuCycleModel::last_label_` | last enqueued label | Enforces ordered, nonduplicate submission. |
| `timing_, events_` | TCU queues | Enqueue checks occupancy at the start of the edge before inserting the whole time point. |

[C++ API](../api.md#timing_controlhpp).

## Reset and errors

Malformed time points, duplicate or stale identities, impossible capacity and
late arrival raise typed faults. The TCU validates the whole transition before
committing its queue changes. A fault in candidate enqueue also suppresses
any output planned by that TCU transition.

Session reset clears the crossing mailboxes and TCU queues. Epoch checks prevent
old work from entering the new session.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [mailbox.hpp](../../include/qsbit/mailbox.hpp).

**CTest:** `core.mailbox`, `control.admission`, `control.atomic`.

The tests check delivery on a strictly later receiver edge and enqueue using
capacity from the start of the edge. Failed validation must leave queues unchanged.
