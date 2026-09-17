# Memory and Response Model

`MemoryModel` services the CPU's instruction and data accesses. It owns the
loaded `ProgramImage` and keeps one pending transaction for each of its two ports.
A store changes memory when its transaction completes.

## Connections

- **Input:** fetch and data requests through separate `MemoryPort` mailboxes.
- **Output:** responses containing the request ID and either a value or an access fault.
- **Scheduling:** `Simulator::memory_edge()` calls the model on each CPU rising edge.
  Memory uses the CPU clock in the current implementation.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="ELF bytes and CPU request"];
  owner [label="MemoryModel"];
  state [label="image_\npending_[0], pending_[1]\nlast_id_"];
  output [label="MemoryResponse or access fault"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Request, completion and response

A request published by the CPU becomes eligible on the next CPU edge. If the
port was idle at the start of that edge and its response mailbox has space, the
memory model accepts it. Completion is scheduled `memory_latency` CPU periods
after acceptance.

When a pending transaction is due and response storage is available, the model
performs the access and publishes its response. The CPU can receive that response
only on a later edge. With 5 ns CPU periods and a one-cycle memory latency, a
request published at 0 ns can be accepted at 5 ns, complete at 10 ns and become
CPU-visible at 15 ns, provided the port and response path remain available.

A port that completes a transaction on an edge cannot accept another on that
same edge. Fetch and data ports make these decisions independently. Their request
IDs must increase, preventing repeated stores and reordered requests.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `image_` | `ProgramImage` | Runtime RAM and segment permissions. |
| `pending_[0], pending_[1]` | optional Pending | Independent fetch/data request, completion tick and epoch. |
| `last_id_` | request IDs | Rejects repeated or out-of-order requests per port. |
| `MemoryPort` | request/response mailboxes | One request slot and one response slot for each CPU port. |

[C++ API](../api.md#memoryhpp).

## Reset and errors

Access checks happen when the memory operation completes. Alignment, range
and permission failures are returned in `MemoryResponse`; the CPU decides when
to expose the fault.

Session reset clears pending transactions and the surrounding port mailboxes.
It preserves the image and all stores completed before reset.

## Implementation and tests

Source: [memory.cpp](../../src/memory.cpp) and [memory.hpp](../../include/qsbit/memory.hpp).

**CTest:** `core.memory`, `systemc.use_cases`.

The tests check memory service times, load/store hazards and access faults,
including their effect on CPU execution.
