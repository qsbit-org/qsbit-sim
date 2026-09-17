# CPU Cycle Model

`CpuCycleModel` executes RV32I programs through three stages: fetch, decode,
and execute/commit. It owns the PC, registers and pipeline state. Memory delays
and blocked quantum instructions can hold the pipeline for several CPU cycles.

Applications can supply another `ICpuCycleModel` through the `Simulator` CPU
factory. The [CPU adapter contract](../cpp-interfaces.md#cpu-adapter) defines that
replacement boundary.

## Connections

- **Input:** fetch and data responses through `CpuPorts`, plus the result of
  calling its control callback.
- **Output:** memory requests, `ProducerOperation` calls, retirement records and
  final architectural state.
- **Scheduling:** `Simulator::cpu_edge()` calls `step()` once per CPU rising edge,
  after the producer has received eligible replies and measurement results.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Memory, producer and feedback inputs"];
  owner [label="CpuCycleModel"];
  state [label="registers_, pc_\nfetch_pc_, fetch_request_\nfetched_, decode_, execute_"];
  output [label="MemoryRequest / ProducerOperation / retirement"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## On a CPU edge

The CPU first accepts an eligible fetch response, then tries to complete the
oldest instruction in execute. A completed instruction updates its destination
register and PC and emits `InstructionRetired`. Register x0 remains zero.

After that retirement, the next instruction can enter execute and capture its
operands. It therefore sees the preceding instruction's new register value.
Each latch advances at most one stage per edge. A load or incomplete control
operation holds execute and prevents younger instructions from advancing into it.

Only the oldest instruction can issue a store or control operation. A blocked
operation keeps the same instruction ID and operands on every retry. QAPPEND
completes when the producer accepts its actions into staging; instructions that
seal a group wait for the completion defined by the
[producer protocol](../module-architecture.md#producer-operations-and-progress).

A taken branch discards younger latches and invalidates outstanding wrong-path
fetches by changing the fetch generation. QEND also clears younger work and
halts the CPU after producer closure. Device work may continue after CPU halt.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `registers_, pc_` | architectural state | Retirement updates the destination and PC; x0 remains zero. |
| `fetch_pc_, fetch_request_` | fetch state | Next fetch address and outstanding request identity/generation. |
| `fetched_, decode_, execute_` | optional Frame latches | Buffered instructions and captured operands, effects or faults. |
| `generation_, halted_` | control state | Invalidates wrong-path fetch replies and stops issue after END. |

[C++ API](../api.md#cpuhpp).

## Reset and errors

Reset clears registers, latches and pending CPU state, then restores the
loaded entry PC. Fetch generations prevent discarded replies from reviving
instructions.

An oldest instruction's fault stops execution before younger effects can
escape. A later TCU or device fault terminates the simulation; it cannot undo an
already retired instruction.

## Implementation and tests

Source: [cpu.cpp](../../src/cpu.cpp) and [cpu.hpp](../../include/qsbit/cpu.hpp).

**CTest:** `systemc.use_cases`, `adapter.normal`, `adapter.reset`.

The tests exercise data hazards, branch flushes and older faults. They also
check unique retirement IDs and construction and reset of a replacement CPU.
