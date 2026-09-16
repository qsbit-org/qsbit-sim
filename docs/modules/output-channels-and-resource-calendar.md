# Output Channels and Resource Calendar

**Architecture position:** [Integrated contract, Section 3.15](../module-architecture.md#315-output-channels-and-device-resource-calendar). **Status:** proposed behavior; no implementation exists yet.

## Responsibility and neighbors

DeviceRuntime is the sole owner of physical channel occupancy and future interval reservations.

| Direction | Contract |
| --- | --- |
| Upstream inputs | TCU launch batch, resolved action intervals and previously scheduled physical boundaries. |
| Downstream outputs | Physical trigger, output start and end, active drive set and event batches to quantum service and readout. |
| State owner and retained state | Resource interval calendar, pending boundaries, active channels, processed batch IDs and session epoch. |

## Module diagram

```mermaid
flowchart LR
    U["Immutable TCU launch batch"] --> P["boundary scheduler and calendar"]
    S[("future intervals; active channels")] <--> P
    P --> D["physical event batch"]
    K["Activation: scheduled edge or event"] -.-> P
```

The dashed activation edge is a scheduling or call relationship. The solid arrows carry records or results. The state shape identifies the only owner of the mutable state; it is not an additional SystemC process.

## Behavior

**Activation:** Scheduled physical-boundary events wake DeviceRuntime; zero-delay launches join the current tick’s explicit phase batch.

**Transition:** Preflight each complete launch batch against existing future reservations and same-batch actions. Install every accepted half-open interval [start,end) once. End old actions before starting new ones at the same tick; allow back-to-back occupancy. Additive drives may overlap if the backend supports them.

**Time and visibility:** DeviceRuntime aggregates all actions assigned tick t before physical-state evaluation. An sc_event only wakes the owner; callback identity and payload remain in durable epoch-tagged storage.

**Reset and errors:** Unsupported conflict rejects the whole batch. Session reset aborts active actions and invalidates scheduled old-epoch callbacks. Positive pulse and acquisition duration is required.

**Focused verification:** Test future overlap, adjacent intervals, zero-delay launch, old-epoch scheduled end and reversed arrival order.

The module uses the baseline [producer and edge-order rules](../module-architecture.md#4-baseline-protocol-and-event-ordering). Any future timing profile that changes these rules must document its own behavior and tests.
