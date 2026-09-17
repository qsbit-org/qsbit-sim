# Memory and Response Model

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Clocked memory owner for runtime requests. ELF loading initializes its storage before the run.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Load-plan bytes, one stable CPU request with ID, and configured memory clock edge (initially the CPU clock). |
| Downstream outputs | Bounded response mailbox with ID, value or access fault. |
| State owner and retained state | Byte storage, pending request slots, configured latency counters and response state. |

## Module diagram

```mermaid
flowchart LR
    U["ELF bytes and CPU request"] --> P["memory edge transition"]
    S[("bytes; pending slots; latency")] <--> P
    P --> D["CPU response or access fault"]
    K["Activation: Memory clock edge"] -.-> P
```

## Behavior

**Activation:** Wake on a configured memory clock edge, initially the CPU clock. Read a committed request; publish a later response according to the profile.

**Transition:** Check address and alignment, accept a request once by identity, schedule read or write completion, and apply a store effect once at its configured completion point. Keep a response stable until consumed; do not have both CPU and memory mutate the same request object.

**Time and visibility:** For a request published at tick p, eligibility begins at the first memory edge strictly after p. Response latency is counted in memory edges and reported as a tick.

**Reset and errors:** Out-of-range and misaligned accesses produce typed faults. Baseline session reset clears in-flight transactions but preserves loaded bytes; cold start reloads them.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Implementation and verification

MemoryModel owns one pending fetch and one pending data transaction; both responses use strict-edge mailboxes.

- Implementation: [memory.cpp](../../src/memory.cpp) and [memory.hpp](../../include/qsbit/memory.hpp).

**CTest:** `core.memory`, `systemc.use_cases`.

Checks memory service and latency-dependent execution, load/store hazards and older access faults.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
