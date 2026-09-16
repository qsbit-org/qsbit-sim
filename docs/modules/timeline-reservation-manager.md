# Timeline Reservation Manager

**Architecture position:** [Module map](../module-architecture.md#3-module-map). **Status:** proposed behavior; no implementation exists yet.

## Responsibility and neighbors

CPU-domain substate that prepares groups of operations for one planned TCU cycle. It does not have its own clocked SystemC process.

| Direction | Contract |
| --- | --- |
| Upstream inputs | APPEND, ADVANCE, FLUSH, READ_RESULT and END from the adapter; TCU group acknowledgments. |
| Downstream outputs | Immutable sealed group, producer acceptance reply, cursor advancement and EndOfStream marker. |
| State owner and retained state | Producer cursor (the logical TCU cycle being planned), initial empty origin, capacity-limited open group, and at most one sealed group awaiting admission. |

## Module diagram

```mermaid
flowchart LR
    U["Ordered producer operations"] --> P["staging and seal state machine"]
    S[("cursor; open group; sealed group")] <--> P
    P --> D["GroupRequest and producer reply"]
    K["Activation: Called within CPU edge"] -.-> P
```

The dashed edge shows what invokes this behavior; it does not add a clock stage. Solid arrows show data flow. The cylinder shows state or read-only configuration used by the behavior, not another SystemC process.

## Behavior

**Activation:** Advance on CPU edges through semantic operations; group replies arrive via a committed mailbox.

**Transition:** APPEND places an event in the open group at the producer cursor and retires on `ProducerAccepted`; a second APPEND may join the same group. ADVANCE(0) changes nothing. Positive ADVANCE freezes a real open group, waits for its matching `GroupAdmitted` reply, then moves the cursor by the requested number of logical TCU cycles. An untouched initial origin needs no submission. FLUSH seals without moving the cursor, so APPEND there remains invalid until a positive ADVANCE. READ_RESULT flushes before waiting; END flushes before closing production.

**Time and visibility:** The group interval is the current producer cursor minus the preceding sealed point’s due cycle, using logical origin zero for the first point; it is not measured from CPU execution or host time. A sealed group remains immutable while its crossing request is held. Admission confirms queue insertion, not physical firing.

**Reset and errors:** Impossible staging or per-port total capacity faults immediately. Reset discards the open and sealed group and returns to the implicit origin with a new epoch.

**Focused verification:** Test same-point APPEND progress, zero wait, initial empty start, wait-only point, append after flush, final-group sealing and oversized-group rejection.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering). A future profile that changes an applicable rule must state the replacement rule and its tests.
