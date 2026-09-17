# Command Crossing and Atomic Admission

A sealed group crosses from the CPU producer to the TCU through a mailbox.
The TCU accepts its timing point and all port events together. Temporary queue
pressure leaves the same group waiting in the mailbox.

## Connections

- **Input:** the producer's sealed `Group` through `ControlLinks::groups`.
- **Output:** entries in the TCU timing and event queues, followed by a `GroupReply`
  through the return mailbox.
- **Scheduling:** the TCU checks an eligible group on a TCU rising edge;
  the CPU receives its reply on a later CPU edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Sealed group mailbox"];
  owner [label="ControlLinks / TcuCycleModel"];
  state [label="groups, replies, closure\nEnvelope\nTcuCycleModel::last_label_"];
  output [label="Queue insertion / GroupReply"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Crossing and acceptance

Publishing a message records its epoch and receiver-eligible tick. Eligibility
starts at the first receiver edge strictly after publication, followed by any
additional configured receiver periods. With TCU edges at 20 and 40 ns and a
one-edge crossing, a group published at 20 ns is first eligible at 40 ns.

The TCU checks the group's manifest, profile fingerprint, ordered label, deadline
and queue requirements. If current occupancy leaves enough room, it inserts the
timing point and every event in one transition. `Simulator` then removes the
mailbox request and publishes its label in `GroupReply`.

Capacity is measured before any entries fire on that edge. Space freed by firing
becomes usable on the next TCU edge. A full queue leaves the candidate in place;
its original deadline continues to approach while it waits.

Admission must occur strictly before the group's due tick. Admission itself
does not launch the group, and a newly admitted group cannot fire on the same
edge. See [crossing and edge order](../module-architecture.md#crossing-and-tcu-edge-order)
for the complete transition.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `groups, replies, closure` | `Mailbox<Group / GroupReply / EndOfStream>` | One-slot crossings between producer and TCU. |
| `Envelope` | `published, eligible, epoch, value` | Retains the payload until its receiver can consume it. |
| `TcuCycleModel::last_label_` | last admitted label | Enforces ordered, nonduplicate submission. |
| `timing_, events_` | TCU queues | Admission checks occupancy at the start of the edge before inserting the whole group. |

[C++ API](../api.md#producerhpp).

## Reset and errors

Malformed groups, duplicate or stale identities, impossible capacity and
late arrival raise typed faults. The TCU validates the whole transition before
committing its queue changes. A fault in candidate admission also suppresses
any launch planned by that TCU transition.

Session reset clears the crossing mailboxes and TCU queues. Epoch checks prevent
old work from entering the new session.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [mailbox.hpp](../../include/qsbit/mailbox.hpp).

**CTest:** `core.mailbox`, `control.admission`, `control.atomic`.

The tests check delivery on a strictly later receiver edge and admission using
capacity from the start of the edge. Failed preflight must leave queues unchanged.
