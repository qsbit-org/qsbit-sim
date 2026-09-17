# CPU Cycle Model

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

One CPU-domain owner, normally wrapped by a clock-sensitive `SC_METHOD`; its pipeline is a replaceable pure C++ cycle model.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Prior committed memory response, producer-operation reply, CPU-visible measurement result with its token, reset epoch and CPU edge. |
| Downstream outputs | Stable memory and control requests, retirement record, PC and GPR state, stall reason and trace records. |
| State owner and retained state | PC, GPRs, pipeline latches, unresolved branch and fault state, held request ID and retirement sequence. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Memory, producer and feedback inputs"];
  owner [label="CpuCycleModel"];
  state [label="registers_, pc_; fetch_pc_, fetch_request_; fetched_, decode_, execute_"];
  output [label="MemoryRequest / ProducerOperation / retirement"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Wake on each CPU rising edge. A separate adapter call inside this transition adds no clock cycle.

**Transition:** Read prior committed inputs; resolve older branch and fault outcomes; advance the chosen pipeline; publish an irreversible store or control operation only from the oldest authorized non-speculative instruction. Hold its ID and operands unchanged during backpressure. Retire APPEND on ProducerAccepted; other operations use their barriers.

**Time and visibility:** A cross-owner reply such as `GroupAdmitted` follows the strict receiver-edge rule. `ProducerAccepted` is a local return inside the CPU-domain transition and adds no crossing latency. The selected CPU timing profile determines retirement; delta cycles never complete an extra pipeline stage.

**Reset and errors:** Reset clears pipeline and register state and restores loaded entry PC. A later protocol failure terminates the run as a simulator fault; it cannot retroactively turn a retired operation into a precise CPU trap.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `registers_, pc_` | `architectural state` | Retirement updates the destination and PC; x0 remains zero. |
| `fetch_pc_, fetch_request_` | `fetch state` | Next fetch address and outstanding request identity/generation. |
| `fetched_, decode_, execute_` | `optional Frame latches` | Buffered instructions and captured operands, effects or faults. |
| `generation_, halted_` | `control state` | Invalidates wrong-path fetch replies and stops issue after END. |

Each step consumes an eligible fetch response, then attempts completion of the oldest execute frame. Completed instructions retire before a younger decode frame captures operands. A taken branch or END clears younger latches and increments the fetch generation. If execute remains occupied, younger work stays held. Data requests are published once and await a matching response.

[Current C++ declarations](../api.md#cpuhpp).

## Implementation and verification

CpuCycleModel implements the three-stage pipeline; Simulator accepts an ICpuCycleModel factory for replacement. adapter_tests.cpp exercises this injection and reset.

- Implementation: [cpu.cpp](../../src/cpu.cpp) and [cpu.hpp](../../include/qsbit/cpu.hpp).

**CTest:** `systemc.use_cases`, `adapter.normal`, `adapter.reset`.

Checks hazards, branch flush, older faults, unique retirement IDs and replaceable CPU construction/reset.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
