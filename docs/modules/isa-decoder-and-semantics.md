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

```mermaid
flowchart LR
    U["Instruction word and operands"] --> P["pure decode and semantics call"]
    S[("immutable ISA tables")] <--> P
    P --> D["decoded effect or fault"]
    K["Activation: Called by CPU pipeline"] -.-> P
```

## Behavior

**Activation:** Call when the CPU model reaches its decode or execute stage, according to the selected pipeline profile.

**Transition:** Decode RV32I, immediates and enabled custom encodings; calculate sign extension, branch target, register effect and exceptions; describe APPEND, ADVANCE, FLUSH, READ_RESULT or END effects without performing them.

**Time and visibility:** Function execution consumes no SystemC time. Pipeline stage timing belongs to the CPU model. The assembler contract verifies emitted machine words against these masks.

**Reset and errors:** Unknown or disabled encodings return illegal instruction. Reset does not change immutable decode tables.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Implementation and verification

rv32::decode and rv32::evaluate implement RV32I and custom-0 validation without SystemC.

- Implementation: [isa.cpp](../../src/isa.cpp) and [isa.hpp](../../include/qsbit/isa.hpp).

**CTest:** `core.isa_arithmetic`, `core.isa_control`, `core.isa_decode`.

Checks RV32I effects, control flow and legal/illegal instruction encodings.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
