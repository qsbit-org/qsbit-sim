# ISA Decoder and Semantics

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Pure C++ library called by the CPU owner. It does not own the PC, GPRs or a SystemC process.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Fetched 32-bit word, PC, operand values, ISA extension profile and privilege assumptions. |
| Downstream outputs | Typed decoded operation, architectural effect description or typed fault; control operations go to the quantum instruction adapter. |
| State owner and retained state | No mutable architectural state. Tables of encoding masks and semantics are immutable for the simulation session. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Instruction word and operands"];
  owner [label="rv32::decode / evaluate"];
  state [label="Decoded; Effect"];
  output [label="Decoded / Effect or typed fault"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="value records / configuration"];
}
```

## Behavior

**Activation:** Call when the CPU model reaches its decode or execute stage, according to the selected pipeline profile.

**Transition:** Decode RV32I, immediates and enabled custom encodings; calculate sign extension, branch target, register effect and exceptions; describe APPEND, ADVANCE, FLUSH, READ_RESULT or END effects without performing them.

**Time and visibility:** Function execution consumes no SystemC time. Pipeline stage timing belongs to the CPU model. The assembler contract verifies emitted machine words against these masks.

**Reset and errors:** Unknown or disabled encodings return illegal instruction. Reset does not change immutable decode tables.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `Decoded` | `value record` | Operation, original word, register fields, immediate and register-use flags. |
| `Effect` | `value record` | Next PC, result, memory request parameters and branch outcome. |

decode maps one machine word to Decoded or raises IllegalInstruction. evaluate takes Decoded plus PC and operand values, and returns Effect. It does not store registers, update the CPU PC, access memory or consume simulated time. Quantum instructions take the producer-adapter path in the CPU.

[Current C++ declarations](../api.md#isahpp).

## Implementation and verification

rv32::decode and rv32::evaluate implement RV32I and custom-0 validation without SystemC.

- Implementation: [isa.cpp](../../src/isa.cpp) and [isa.hpp](../../include/qsbit/isa.hpp).

**CTest:** `core.isa_arithmetic`, `core.isa_control`, `core.isa_decode`.

Checks RV32I effects, control flow and legal/illegal instruction encodings.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
