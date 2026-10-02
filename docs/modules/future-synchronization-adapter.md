# Synchronization

sync reserves an instruction encoding for distributed synchronization.
The current simulator recognizes that encoding and raises
`UnsupportedSynchronization` when it executes. Distributed synchronization is
not implemented.

## Connections

- **Input:** a legally encoded sync at the CPU's oldest execute stage.
- **Output:** a typed fault recorded by `Simulator`.
- **Scheduling:** rejection occurs in the timing control call on a CPU edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="sync"];
  owner [label="TimingControl rejection"];
  state [label="No retained synchronization state"];
  output [label="UnsupportedSynchronization"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="state"];
}
```

## Current behavior

`adapt_quantum()` converts sync to `ControlKind::Synchronize`.
`TimingControl::execute()` raises `UnsupportedSynchronization` without
completing the instruction or creating a time point. The simulator then
stops with failure.

## Objects and state

The decoded instruction and `ControlOperation` are temporary values.
No synchronization state is retained.

[C++ API](../api.md#timing_controlhpp).

## Reset and errors

Malformed sync encodings fail earlier as `IllegalInstruction`. Valid
encodings reach `UnsupportedSynchronization`. Session reset has no
synchronization-specific state to clear.

## Implementation and tests

Source: [timing_control.cpp](../../src/timing_control.cpp), [timing_control.hpp](../../include/qsbit/timing_control.hpp).

**CTest:** `systemc.use_cases`.

The `unsupported` program executes sync and requires `UnsupportedSynchronization`.
