# CPU cycle model

`CpuCycleModel` executes RV32I and qsbit scalar control instructions.
`VliwCpuCycleModel` also executes 32-bit dual-codeword bundles. Both use
fetch, decode and execute stages, with results committed in execute.
Memory delays and blocked control instructions hold the pipeline while the
TCU continues running.

Select the implementation with
[`--cpu-model`](../interfaces.md#cpu-selection). Both fetch one 32-bit word
per request and use the same memory and timing-control interfaces.

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
  owner [label="CPU cycle model"];
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
Retries keep the same instruction ID and operands. `cw` completes when
timing control stores the mapped events in the current time point;
other control instructions follow the
[instruction completion rules](../module-architecture.md#reserve-phase-operations-and-progress).

A taken branch discards younger instructions and changes the fetch
generation to reject their pending replies. The exit ECALL also discards younger
work and halts the CPU after publishing closure. Simulation completion follows the
[drain conditions](../module-architecture.md#closure-and-drain).

`CpuPipelineUpdated` records the outstanding fetch, fetch buffer, decode slot,
execute slot and halt state at the end of an edge when they change. The
[snapshot fields](../interfaces.md#cpu-pipeline-snapshots) identify each occupied
slot by instruction ID and PC. `InstructionRetired` records commitment from
execute on the same edge.

## Dual-codeword execution

`VliwCpuCycleModel` captures both operations' operands when the bundle enters
execute. `decode_cw_bundle()` converts each 12-bit operation to a scalar
`cw` encoding and applies the shared decoder and quantum instruction adapter.

On an execute edge, the CPU submits operation 0, then operation 1. If both
are accepted, the bundle retires on that edge. If either blocks, execute
retains the captured operands and retries the first unaccepted operation
on a later edge. An accepted operation is not submitted again.

The bundle produces one `InstructionRetired` record. Its two
`CodewordQueued` records carry the same instruction ID and planned time
point. The TCU triggers their mapped events at that time point.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `registers_, pc_` | architectural state | Retirement updates the destination and PC; x0 remains zero. |
| `fetch_pc_, fetch_request_` | fetch state | Next fetch address and outstanding request identity and generation. |
| `fetched_, decode_, execute_` | optional Frame latches | Buffered instructions and captured operands, effects or faults. |
| `generation_, halted_` | control state | Invalidates wrong-path fetch replies and stops issue after exit ECALL. |
| VLIW frame `operations, completed_lanes` | Two captured operations and an accepted-operation count | Preserves progress while a bundle is blocked. |

[C++ API](../api.md#cpuhpp).

## Reset and errors

Reset clears registers, latches and pending CPU state, then restores the
loaded entry PC. Fetch generations prevent discarded replies from reviving
instructions.

A fault in the oldest instruction stops execution before younger instructions
can issue stores or control operations. A later TCU or device fault terminates the simulation; it cannot undo an
already retired instruction.

## Implementation and tests

The [shared pipeline](../../src/cpu/pipeline.cpp) implements fetch, execution,
memory access, retirement and tracing. The
[bundle decoder](../../src/cpu/vliw.cpp) captures both lanes' operands.
Declarations:
[rv32.hpp](../../include/qsbit/cpu/rv32.hpp) and
[vliw.hpp](../../include/qsbit/cpu/vliw.hpp).

**CTest:** `cpu.trace`, `cpu.vliw`, `systemc.vliw`, `systemc.use_cases`, `adapter.normal`, `adapter.reset`.

The scalar use cases exercise data hazards, branch flushes, feedback and
reset in the shared pipeline. Bundle tests check operand modes, reserved
bits, partial acceptance, resource conflicts and event timing across clock
phases and process registration orders. Adapter tests check construction
and reset of a replacement CPU.
Pipeline trace tests cover occupied slots, held instructions, branch flushes,
halt, reset and core identity for both built-in CPU models.
