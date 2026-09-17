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

```mermaid
flowchart LR
    U["TCU atomic event admission"] --> P["manifest match by label"]
    S[("per-port FIFO bank")] <--> P
    P --> D["complete due candidate batch"]
    K["Activation: Called within TCU edge"] -.-> P
```

## Behavior

**Activation:** Admission appends on a TCU edge; label broadcast reads old heads in that owner transition.

**Transition:** When the timer reaches label L, gather matching-label events from the previously committed queue heads and compare their IDs with the timing point’s manifest. Only a complete match becomes one candidate launch batch; remove those members once. Ports absent from the list stay idle and future-label entries remain queued. The configured per-port firing width cannot be bypassed with zero-time loops.

**Time and visibility:** Logical matching and launch preflight share a TCU edge; separate C++ helper calls do not add clock cycles.

**Reset and errors:** Past or extra same-label entries fault with the whole group before launch. Reset clears every FIFO and invalidates old epoch entries.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Implementation and verification

TcuCycleModel owns the per-port deque bank and verifies every manifested event before atomic dequeue.

- Implementation: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.atomic`.

Checks simultaneous port members, old queue credits and atomic dequeue failure.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
