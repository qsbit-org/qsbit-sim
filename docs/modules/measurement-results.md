# Measurement results and CPU feedback

`MeasurementResults` reserves a slot for each accepted measurement and retains
it until QREAD consumes the result. A generation counter distinguishes
successive uses of the same slot.

## Connections

- **Input:** a measurement reservation from the timing control, a tagged CPU completion,
  or a QREAD handle.
- **Output:** a measurement reference and 32-bit handle, a result bit, or an incomplete read.
- **Scheduling:** reservation and consumption occur in the CPU control call.
  `TimingControl::receive()` delivers arrived results before the CPU steps.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Acquisition acceptance and completion"];
  owner [label="MeasurementResults"];
  state [label="slots_\ngenerations_\nfast_pending_"];
  output [label="MeasurementReference and visible result and released slot"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Slot lifecycle

`reserve()` changes a slot from **Free** to **Pending** and assigns a
`MeasurementReference`. The program receives its 32-bit handle.

`deliver()` checks the full reference and changes the slot to **Visible**.
QREAD first enqueues any pending events, then waits for its slot to become
Visible. It returns the bit and frees the slot.

Different measurements can complete out of order. Each delivery must match
the reserved epoch, measurement ID, slot generation, handle and target.

Fast-feedback credits are tracked separately. QREAD frees the CPU slot;
a TCU delivery acknowledgment frees the fast-feedback credit. Successful
completion waits for deliveries and acknowledgments, but allows unread
Visible slots.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `slots_` | `vector<Slot>` | Each slot has Free, Pending or Visible state, measurement reference and result bit. |
| `generations_` | per-slot generation | Persists across reset so a reused slot does not revive an old handle. |
| `fast_pending_` | `map<measurement ID, MeasurementReference>` | Independent fast-delivery credits, released by acknowledgment. |
| `next_measurement_` | checked ID | Next measurement identity within the epoch. |

[C++ API](../api.md#feedbackhpp).

## Reset and errors

Unknown, consumed, wrong-generation or wrong-epoch handles fail. A full
CPU slot array causes QAPPEND to fail rather than wait for a later QREAD that
cannot execute while QAPPEND is blocked.

Reset clears slots and pending fast credits and restarts measurement IDs in a
new epoch. Per-slot generation counters survive reset, so reusing a slot cannot
make an old handle valid again.

## Implementation and tests

Source: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.scoreboard`, `protocol.readout`.

The tests check result consumption and slot reuse, reject duplicate or stale
results, and verify that CPU consumption and fast delivery release their
respective resources independently.
