# CPU cycle model

`CpuCycleModel` executes RV32I programs through fetch, decode and execute stages.
The execute stage commits results. Memory delays and blocked control instructions
hold the pipeline while the TCU continues running.

Applications can supply another `ICpuCycleModel` through the `Simulator` CPU
factory. The [CPU adapter contract](../cpp-interfaces.md#cpu-adapter) defines that
replacement boundary.

## Connections

- **Input:** fetch and data responses through `CpuPorts`, plus the result of
  calling its control callback.
- **Output:** memory requests, `ControlOperation` calls, retirement records and
  final architectural state.
- **Scheduling:** `Simulator::cpu_edge()` calls `step()` once per CPU rising edge,
  after timing control has received replies and measurement results.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Memory, timing control and feedback inputs"];
  owner [label="CpuCycleModel"];
  state [label="registers_, pc_\nfetch_pc_, fetch_request_\nfetched_, decode_, execute_"];
  output [label="MemoryRequest and ControlOperation and retirement"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## On a CPU edge

The CPU receives any arrived fetch response, then attempts to complete
the oldest instruction in execute. Retirement updates its register and PC
and emits `InstructionRetired`. Register x0 remains zero.

A younger instruction entering execute then captures its operands, including
values retired on that edge. Each instruction advances at most one stage
per edge. A load or blocked control instruction holds execute and prevents
younger instructions from entering it.

Only the oldest instruction can issue a store or control operation.
Retries keep the same instruction ID and operands. cw completes when
timing control accepts its events; other control instructions follow the
[instruction completion rules](../module-architecture.md#reserve-phase-operations-and-progress).

A taken branch discards younger instructions and changes the fetch
generation to reject their pending replies. exit ECALL also discards younger
work and halts the CPU after publishing closure. The simulation continues
until queued work and result deliveries finish.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `registers_, pc_` | architectural state | Retirement updates the destination and PC; x0 remains zero. |
| `fetch_pc_, fetch_request_` | fetch state | Next fetch address and outstanding request identity and generation. |
| `fetched_, decode_, execute_` | optional Frame latches | Buffered instructions and captured operands, effects or faults. |
| `generation_, halted_` | control state | Invalidates wrong-path fetch replies and stops issue after exit ECALL. |

[C++ API](../api.md#cpuhpp).

## Reset and errors

Reset clears registers, latches and pending CPU state, then restores the
loaded entry PC. Fetch generations prevent discarded replies from reviving
instructions.

A fault in the oldest instruction stops execution before younger instructions
can issue stores or control operations. A later TCU or device fault terminates the simulation; it cannot undo an
already retired instruction.

## Implementation and tests

Source: [rv32.cpp](../../src/cpu/rv32.cpp) and [rv32.hpp](../../include/qsbit/cpu/rv32.hpp).

**CTest:** `systemc.use_cases`, `adapter.normal`, `adapter.reset`.

The tests exercise data hazards, branch flushes and older faults. They also
check unique retirement IDs and construction and reset of a replacement CPU.
