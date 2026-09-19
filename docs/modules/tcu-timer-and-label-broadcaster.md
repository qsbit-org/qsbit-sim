# TCU Timer and Label Broadcaster

The TCU timer determines when admitted groups fire. On each TCU rising edge,
it checks whether the timing-queue head is due and uses its label to select the
corresponding port events.

## Connections

- **Input:** current tick, configured start and TCU period, and the existing
  timing-queue head.
- **Output:** a validated `LaunchBatch` and `LabelFired` record when a group is due.
- **Scheduling:** `TcuCycleModel::step()` runs on each TCU rising edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TCU clock and timing head"];
  owner [label="TcuCycleModel"];
  state [label="start_\ncycle (local variable)\ntiming_.front().due"];
  output [label="TcuOutput / LabelFired"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="state and derived values"];
}
```

## From logical cycle to global tick

Let the effective start tick of the current epoch be S and the TCU period be P. Logical cycle n occurs at
`S + n * P`; cycle zero is the start edge. Before S, groups can enter the queues
but cannot fire. The current cycle is calculated on each call rather than stored
as an incrementing counter.

For example, S = 100 ns and P = 20 ns place cycle 4 at 180 ns. A group admitted
at 160 ns can fire there. Admission at 180 ns is too late. These example values
are separate from the [default profile](../implementation.md#default-timing).

The due label selects the complete event group. Manifest checks, condition
evaluation and device preflight happen within this TCU transition before queue
removal. A new admission on this edge cannot become its firing candidate.

The timer continues through empty queues and CPU stalls. It neither waits for
the next group nor shifts later deadlines. Incoming fast results are committed
after the firing decision and become usable on later edges.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `start_` | global tick | Start of the current epoch timeline. |
| Local variable `cycle` | derived logical cycle | Calculated from current tick and period; not a stored running counter. |
| `timing_.front().due` | next due cycle | Selects the group already queued when the edge begins. |
| `closed_` | closure state | Input closed after validated EndOfStream; queues may still drain. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

A queued point that misses its due edge or a group admitted too late raises
`LateAdmission`. Time arithmetic is checked for overflow.

Reset clears the TCU queues and history. The new start is the first TCU edge at
or after `reset_tick + profile.start`. SystemC time continues from the reset tick.
The current TCU has no pause/resume interface; QSYNC is unsupported.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.empty`, `systemc.tcu_trace`.

`control.empty` asserts cumulative launch ticks across empty gaps and late-admission rejection. `systemc.tcu_trace` checks that its clocked harness drains and writes the label trace.
