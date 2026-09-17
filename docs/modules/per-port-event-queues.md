# Per-Port Event Queues

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Bounded queue bank owned by the same TCU cycle model as the timing queue and broadcaster.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Atomically admitted resolved events grouped by timing label and physical port. |
| Downstream outputs | All manifested head events for a due label to launch preflight; occupancy to admission. |
| State owner and retained state | Per-port ordered FIFOs, head IDs and configured per-port firing width. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TCU atomic event admission"];
  owner [label="TcuCycleModel::events_"];
  state [label="events_; ReservedEvent::label / id; Profile::event_capacity / firing_width"];
  output [label="Matching-label event members"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Admission appends on a TCU edge; label broadcast reads old heads in that owner transition.

**Transition:** When the timer reaches label L, gather matching-label events from the previously committed queue heads and compare their IDs with the timing point’s manifest. Only a complete match becomes one candidate launch batch; remove those members once. Ports absent from the list stay idle and future-label entries remain queued. The configured per-port firing width cannot be bypassed with zero-time loops.

**Time and visibility:** Logical matching and launch preflight share a TCU edge; separate C++ helper calls do not add clock cycles.

**Reset and errors:** Past or extra same-label entries fault with the whole group before launch. Reset clears every FIFO and invalidates old epoch entries.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `events_` | `vector<deque<ReservedEvent>>` | One FIFO per physical output port. |
| `ReservedEvent::label / id` | `group/member identities` | Matches queued members to the timing-head manifest. |
| `Profile::event_capacity / firing_width` | `bounds` | Storage and same-point output limits per port. |

Atomic admission places each event in action.port. On firing, matching-label prefixes are gathered and checked against the timing manifest. Future-label members stay queued. After preflight succeeds, all members of the fired label are removed together; false-condition events are consumed but omitted from the launch batch.

[Current C++ declarations](../api.md#tcuhpp).

## Implementation and verification

TcuCycleModel owns the per-port deque bank and verifies every manifested event before atomic dequeue.

- Implementation: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.atomic`.

Checks simultaneous port members, old queue credits and atomic dequeue failure.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
