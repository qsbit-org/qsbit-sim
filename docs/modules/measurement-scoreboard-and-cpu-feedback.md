# Measurement Scoreboard and CPU Feedback

The scoreboard reserves a result slot when the producer accepts a measurement.
It tracks that measurement until its bit reaches the CPU and QREAD consumes it.
Each slot carries a token so delayed results cannot overwrite a reused slot.

## Connections

- **Input:** a measurement reservation from the producer, a tagged CPU completion,
  or a QREAD handle.
- **Output:** a token and 32-bit handle, a result bit, or an incomplete read.
- **Scheduling:** reservation and consumption occur in the CPU control call.
  `TimelineProducer::receive()` delivers eligible results before the CPU steps.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Acquisition acceptance and completion"];
  owner [label="Scoreboard"];
  state [label="slots_\ngenerations_\nfast_pending_"];
  output [label="Token / visible result / released slot"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Slot lifecycle

A slot moves from **Free** to **Pending** when `reserve()` assigns its token.
The token includes epoch, measurement ID, slot, generation, handle and target.
The returned handle is what the program stores in a register.

`deliver()` checks the full token and changes Pending to **Visible**. A QREAD
first flushes any open group, then waits if its slot is still Pending. Once
Visible, consumption returns the bit and makes the slot Free.

Acquisition end, result readiness, CPU visibility and consumption are separate
events. A result ready on a CPU edge crosses to a strictly later edge, even with
zero discriminator delay. Different tokens can complete out of order.

Fast-feedback delivery has separate credits. Consuming a CPU result does not
release a pending fast credit; the TCU's delivery acknowledgment does. Successful
simulation completion waits for those deliveries, but it may retain unread
Visible CPU slots as final state.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `slots_` | `vector<Slot>` | Each slot has Free, Pending or Visible state, token and result bit. |
| `generations_` | per-slot generation | Persists across reset so a reused slot does not revive an old handle. |
| `fast_pending_` | `map<measurement ID, Token>` | Independent fast-delivery credits, released by acknowledgment. |
| `next_measurement_` | checked ID | Next measurement identity within the epoch. |

[C++ API](../api.md#feedbackhpp).

## Reset and errors

Unknown, consumed, wrong-generation or wrong-epoch handles fail. A full
CPU slot array causes APPEND to fail rather than wait for a later QREAD that
cannot execute while APPEND is blocked.

Reset clears slots and pending fast credits and restarts measurement IDs in a
new epoch. Per-slot generation counters survive reset, so reusing a slot cannot
make an old handle valid again.

## Implementation and tests

Source: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.scoreboard`, `protocol.readout`.

The tests check result consumption and slot reuse, reject duplicate or stale
results, and verify that CPU consumption and fast delivery release their
respective resources independently.
