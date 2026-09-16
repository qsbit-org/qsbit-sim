# Operation Lowerer and Device Distributor

**Architecture position:** [Integrated contract, Section 3.7](../module-architecture.md#37-optional-lowerer-device-event-distributor-and-configuration-store). **Status:** proposed behavior; no implementation exists yet.

## Responsibility and neighbors

Pure producer-side group resolver. Mandatory port action resolution is present even if optional higher-level lowering is disabled.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Open same-time group, versioned codeword map, target map and profile hash. |
| Downstream outputs | Immutable per-port event group with resolved resources, delays, durations and measurement associations. |
| State owner and retained state | Configuration tables are read-only per epoch; transient grouping and duplicate checks occur within the producer owner. |

## Module diagram

```mermaid
flowchart LR
    U["Open producer group and codeword map"] --> P["resolve, combine and distribute"]
    S[("immutable config; transient group")] <--> P
    P --> D["resolved per-port events"]
    K["Activation: pure call"] -.-> P
```

The dashed activation edge is a scheduling or call relationship. The solid arrows carry records or results. The state shape identifies the only owner of the mutable state; it is not an additional SystemC process.

## Behavior

**Activation:** APPEND checks each action before producer acceptance; sealing performs whole-group combination before crossing submission.

**Transition:** Resolve codewords to action descriptors, combine simultaneous actions, detect duplicates and route actions to target device ports before TCU admission. Baseline direct port-codeword commands do not require eQASM decoding. Higher-level multi-point lowering needs a separately declared bounded timing profile.

**Time and visibility:** Pure lookup itself adds no modeled latency. Any future microcode pipeline with finite issue bandwidth must declare its stage timing and queue demand.

**Reset and errors:** Missing mapping, incompatible port or impossible group width fails before admission. Configuration does not change during an epoch.

**Focused verification:** Check two same-label ports, duplicate physical target, invalid codeword, profile hash mismatch and an expansion that would reorder timeline points.

The module uses the baseline [producer and edge-order rules](../module-architecture.md#4-baseline-protocol-and-event-ordering). Any future timing profile that changes these rules must document its own behavior and tests.
