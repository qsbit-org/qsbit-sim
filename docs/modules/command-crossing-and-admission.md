# Command Crossing and Atomic Admission

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

The mailbox holds one sealed group crossing from the CPU domain; the TCU-domain owner decides whether to insert it into its queues. The mailbox cannot mutate those queues by itself.

| Direction | Contract |
| --- | --- |
| Upstream inputs | One sealed group from the producer and an optional prior admission reply. |
| Downstream outputs | Atomic timing and event queue writes, GroupReply acknowledgment or typed error. |
| State owner and retained state | One held request, epoch and group IDs, crossing visibility tick, reply mailbox and de-duplication state. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Sealed group mailbox"];
  owner [label="ControlLinks / TcuCycleModel"];
  state [label="groups, replies, closure; Envelope; TcuCycleModel::last_label_"];
  output [label="Queue insertion / GroupReply"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** TCU admission check runs on its rising edge only after the request passes configured receiver-edge latency.

**Transition:** Check the sealed group’s member list, due-cycle deadline, queue capacity and IDs. If every check passes, insert one timing entry and all required per-port entries together, then return an ID-matched `GroupReply` acknowledgment. If only current occupancy blocks insertion, keep the same request pending; never insert a partial group.

**Time and visibility:** At edge E, use old occupancy. While the timer runs, require the group’s due cycle to be later than `T_D`; before start, require admission strictly before its mapped due tick. An entry admitted at E cannot fire at E; a freed slot becomes usable on the next TCU edge.

**Reset and errors:** Reject stale epoch, duplicate group, impossible total capacity and late due cycle. Session reset clears mailboxes; old callbacks remain barred by epoch.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `groups, replies, closure` | `Mailbox<Group / GroupReply / EndOfStream>` | One-slot crossings between producer and TCU. |
| `Envelope` | `published, eligible, epoch, value` | Durable payload and computed receiver visibility. |
| `TcuCycleModel::last_label_` | `last admitted label` | Enforces ordered, nonduplicate submission. |
| `timing_, events_` | `TCU queues` | Admission checks old occupancy before inserting all members. |

publish computes eligibility from the first receiver edge strictly after publication. peek leaves a blocked group in its mailbox. A successful TCU transition returns admitted=true; Simulator takes the group and publishes GroupReply. Temporary occupancy stalls; impossible capacity, stale identity or a missed deadline raises a fault.

[Current C++ declarations](../api.md#producerhpp).

## Implementation and verification

ControlLinks carries committed groups and replies. TcuCycleModel checks complete-group capacity using old occupancy.

- Implementation: [tcu.cpp](../../src/tcu.cpp) and [mailbox.hpp](../../include/qsbit/mailbox.hpp).

**CTest:** `core.mailbox`, `control.admission`, `control.atomic`.

Checks strict-edge delivery, old occupancy, complete-group admission and unchanged queues on preflight failure.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
