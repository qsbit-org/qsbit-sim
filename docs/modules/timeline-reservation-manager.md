# Timeline Reservation Manager

`TimelineProducer` prepares operations for future TCU cycles. It collects
actions that should fire together, seals them into one group, and waits for the
TCU to accept that group.

Its **cursor** is the logical TCU cycle being prepared. The cursor is independent
of the running TCU timer: the CPU can prepare cycle 8 while the TCU is still
waiting to start.

## Connections

- **Input:** ordered `ProducerOperation` calls, admission replies, CPU measurement
  completions and returned fast-feedback credits.
- **Output:** a completed instruction result, a sealed `Group`, or `EndOfStream`
  through `ControlLinks`.
- **Scheduling:** `Simulator::cpu_edge()` receives eligible replies before the CPU
  attempts its next producer operation.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Ordered producer operations"];
  owner [label="TimelineProducer"];
  state [label="cursor_, last_admitted_due_\nopen_events_, open_, flushed_\nsealed_"];
  output [label="Group / EndOfStream / CPU completion"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Preparing and submitting a group

APPEND resolves a command, checks its capacity requirements and adds its
actions to the open group at the cursor. A measurement reserves a result slot
at this point. The instruction can then retire, allowing another APPEND to add
actions for the same cycle.

Positive ADVANCE seals an open group and waits for its matching `GroupReply`.
After the reply arrives, it moves the cursor by the requested number of TCU
cycles and opens the next group. ADVANCE(0) leaves both cursor and group unchanged.
FLUSH seals without moving the cursor. APPEND after FLUSH requires a positive
ADVANCE first.

For example, APPEND(A), APPEND(B), ADVANCE(3) at cursor 4 submits A and B together
for cycle 4. The cursor becomes 7 only after admission is acknowledged. None of
these operations changes the TCU timer or advances SystemC time directly.

READ_RESULT flushes before waiting for a CPU-visible measurement. END flushes
before publishing closure. The producer holds the instruction ID and operands
across retries and permits only one sealed submission at a time.

At startup, an untouched empty origin needs no submission. After a positive
ADVANCE opens a point, sealing that point submits an entry even if it has no
events. Such an entry is a wait-only point. The
[producer protocol](../module-architecture.md#producer-operations-and-progress)
defines all completion cases.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `cursor_, last_admitted_due_` | logical TCU cycles | Current planned cycle and the last acknowledged due cycle. Their difference gives the next interval. |
| `open_events_, open_, flushed_` | open-group state | Staged events and whether the point remains appendable. |
| `sealed_` | optional Group | One immutable submission awaiting the matching label reply. |
| `held_` | optional ProducerOperation | Keeps the instruction ID and operands unchanged while an operation waits. |
| `last_label_, next_event_, closed_` | identity and lifecycle | Allocates increasing label and event IDs and records producer closure. |

[C++ API](../api.md#producerhpp).

## Reset and errors

A group that exceeds staging, per-port width or total destination capacity
fails immediately. A full CPU result-slot array also fails: only a later
READ_RESULT could free it, so blocking the current APPEND would prevent progress.
The producer can wait for fast-feedback credits because earlier measurements
can release them independently.

Reset clears staged and sealed groups, the held operation and closure state.
It returns the cursor to zero and restarts IDs in the new epoch.

## Implementation and tests

Source: [producer.cpp](../../src/producer.cpp) and [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `protocol.producer`, `protocol.capacity`, `systemc.use_cases`.

The tests check that a blocked operation keeps its identity and submits only
once, that the cursor moves after acknowledgment, and that staging stays bounded.
They also check that ADVANCE(0) leaves multiple APPENDs in the same group.
