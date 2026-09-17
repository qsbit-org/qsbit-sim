# Synchronization Extension Boundary

QSYNC reserves an instruction encoding for distributed synchronization.
The current simulator recognizes that encoding and raises
`UnsupportedSynchronization` when it executes. Distributed synchronization is
not implemented.

## Connections

- **Input:** a legally encoded QSYNC at the CPU's oldest execute stage.
- **Output:** a typed fault recorded by `Simulator`.
- **Scheduling:** rejection occurs in the producer call on a CPU edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="QSYNC"];
  owner [label="TimelineProducer rejection"];
  state [label="No retained synchronization state"];
  output [label="UnsupportedSynchronization"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="state"];
}
```

## Current behavior

The adapter converts QSYNC to `ProducerKind::Synchronize`.
`TimelineProducer::execute()` raises the fault without completing the instruction.
It creates no timing point and leaves the TCU timer running.

A distributed implementation would need peer-message timing, synchronization
booking, pause/resume behavior and reset handling. During a local TCU pause,
global time and physical quantum evolution would still have to continue.
There are no peer queues or pause state in the current implementation.

## Objects and state

The decoded instruction and `ProducerOperation` are temporary values.
The current implementation stores no synchronization state.

[C++ API](../api.md#producerhpp).

## Reset and errors

Malformed QSYNC encodings fail earlier as `IllegalInstruction`. Valid
encodings reach `UnsupportedSynchronization`. Session reset has no
synchronization-specific state to clear.

## Implementation and tests

Source: [producer.cpp](../../src/producer.cpp), [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `systemc.use_cases`.

The `unsupported` program executes QSYNC and requires `UnsupportedSynchronization`.
