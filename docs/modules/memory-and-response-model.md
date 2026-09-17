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

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="ELF bytes and CPU request"];
  owner [label="MemoryModel"];
  state [label="image_; pending_[0], pending_[1]; last_id_"];
  output [label="MemoryResponse or access fault"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Wake on a configured memory clock edge, initially the CPU clock. Read a committed request; publish a later response according to the profile.

**Transition:** Check address and alignment, accept a request once by identity, schedule read or write completion, and apply a store effect once at its configured completion point. Keep a response stable until consumed; do not have both CPU and memory mutate the same request object.

**Time and visibility:** For a request published at tick p, eligibility begins at the first memory edge strictly after p. Response latency is counted in memory edges and reported as a tick.

**Reset and errors:** Out-of-range and misaligned accesses produce typed faults. Baseline session reset clears in-flight transactions but preserves loaded bytes; cold start reloads them.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `image_` | `ProgramImage` | Runtime RAM and segment permissions. |
| `pending_[0], pending_[1]` | `optional Pending` | Independent fetch/data request, completion tick and epoch. |
| `last_id_` | `request IDs` | Rejects repeated or out-of-order requests per port. |
| `MemoryPort` | `request/response mailboxes` | One request slot and one response slot for each CPU port. |

On each memory edge, complete a due old transaction if its response mailbox has space. Accept a new request only if the pending slot was already idle at the start of the edge and the response mailbox has space. A completion cannot free a slot for same-edge reuse. A write takes effect at completion. The response is published at that tick and becomes CPU-visible on a later strict edge. Reset clears pending state and port mailboxes while retaining memory bytes.

[Current C++ declarations](../api.md#memoryhpp).

## Implementation and verification

MemoryModel owns one pending fetch and one pending data transaction; both responses use strict-edge mailboxes.

- Implementation: [memory.cpp](../../src/memory.cpp) and [memory.hpp](../../include/qsbit/memory.hpp).

**CTest:** `core.memory`, `systemc.use_cases`.

Checks memory service and latency-dependent execution, load/store hazards and older access faults.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
