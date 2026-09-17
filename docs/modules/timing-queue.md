# Timing Queue

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Bounded FIFO substate of one TCU cycle model. It has no separate `SC_METHOD` or delta-cycle notification contract.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Atomically admitted TimingPoint records with interval, label and exact member manifest. |
| Downstream outputs | Head point and cumulative due cycle to the TCU timer; occupancy to admission. |
| State owner and retained state | FIFO entries, last admitted due cycle and head manifest. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Atomic TimingPoint admission"];
  owner [label="TcuCycleModel::timing_"];
  state [label="Point::point; Point::due; last_due_"];
  output [label="Head TimingPoint and cumulative due cycle"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Admission appends on a TCU edge; the timer inspects and removes a due old head in that same owner transition.

**Transition:** Keep admitted timing points in producer order. Each entry carries an interval, label and expected-member list. Add its interval to the prior logical due cycle; do not restart timing from the admission tick, even if the queue was empty meanwhile. An empty member list is an intentional wait-only point; an empty FIFO alone is not a fault.

**Time and visibility:** Only the old head may fire on the current edge. New entries cannot be inspected as due until a later edge. Empty queue does not pause T_D or rebase a later entry.

**Reset and errors:** Late admission faults; a manifested missing member causes ManifestMismatch at firing. Reset empties the FIFO and invalidates its epoch.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `Point::point` | `TimingPoint` | Epoch, label, interval and exact event-ID manifest. |
| `Point::due` | `logical TCU cycle` | Cumulative due cycle calculated during admission. |
| `last_due_` | `logical TCU cycle` | Last admitted due cycle, retained even when the FIFO empties. |

Admission computes new_due = last_due + interval and appends one Point. At its due edge the TCU validates the complete group before popping the timing head. Empty gaps leave last_due unchanged, so later arrivals cannot rebase the timeline. A firing edge uses old occupancy for candidate admission.

[Current C++ declarations](../api.md#tcuhpp).

## Implementation and verification

TcuCycleModel owns the bounded timing FIFO and cumulative due cycles. It does not use an absolute-time heap for TCU scheduling.

- Implementation: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.empty`.

Checks timing FIFO capacity, old credits, cumulative intervals and empty-gap deadline preservation.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
