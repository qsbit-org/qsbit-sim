# Synchronization

QSYNC reserves an instruction encoding for distributed synchronization.
The current simulator recognizes that encoding and raises
`UnsupportedSynchronization` when it executes. Distributed synchronization is
not implemented.

## Connections

- **Input:** a legally encoded QSYNC at the CPU's oldest execute stage.
- **Output:** a typed fault recorded by `Simulator`.
- **Scheduling:** rejection occurs in the timing control call on a CPU edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="QSYNC"];
  owner [label="TimingControl rejection"];
  state [label="No retained synchronization state"];
  output [label="UnsupportedSynchronization"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="state"];
}
```

## Current behavior

`adapt_quantum()` converts QSYNC to `ControlKind::Synchronize`.
`TimingControl::execute()` raises `UnsupportedSynchronization` without
completing the instruction or creating a time point. The simulator then
stops with failure.

## Objects and state

The decoded instruction and `ControlOperation` are temporary values.
No synchronization state is retained.

[C++ API](../api.md#producerhpp).

## Reset and errors

Malformed QSYNC encodings fail earlier as `IllegalInstruction`. Valid
encodings reach `UnsupportedSynchronization`. Session reset has no
synchronization-specific state to clear.

## Implementation and tests

Source: [producer.cpp](../../src/producer.cpp), [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `systemc.use_cases`.

The `unsupported` program executes QSYNC and requires `UnsupportedSynchronization`.
