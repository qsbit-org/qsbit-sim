# ISA Decoder and Semantics

**Architecture position:** [Integrated contract, Section 3.3](../module-architecture.md#33-isa-decoder-and-semantics-library). **Status:** proposed behavior; no implementation exists yet.

## Responsibility and neighbors

Pure C++ library called by the CPU owner. It does not own the PC, GPRs or a SystemC process.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Fetched 32-bit word, PC, operand values, ISA extension profile and privilege assumptions. |
| Downstream outputs | Typed decoded operation, architectural effect description or typed fault; control operations go to the quantum instruction adapter. |
| State owner and retained state | No mutable architectural state. Tables of encoding masks and semantics are immutable for one run. |

## Module diagram

```mermaid
flowchart LR
    U["Instruction word and operands"] --> P["pure decode and semantics call"]
    S[("immutable ISA tables")] <--> P
    P --> D["decoded effect or fault"]
    K["Activation: pure call"] -.-> P
```

The dashed activation edge is a scheduling or call relationship. The solid arrows carry records or results. The state shape identifies the only owner of the mutable state; it is not an additional SystemC process.

## Behavior

**Activation:** Call when the CPU model reaches its decode or execute stage, according to the selected pipeline profile.

**Transition:** Decode RV32I, immediates and enabled custom encodings; calculate sign extension, branch target, register effect and exceptions; describe APPEND, ADVANCE, FLUSH, READ_RESULT or END effects without performing them.

**Time and visibility:** Function execution consumes no SystemC time. Pipeline stage timing belongs to the CPU model. The assembler contract verifies emitted machine words against these masks.

**Reset and errors:** Unknown or disabled encodings return illegal instruction. Reset does not change immutable decode tables.

**Focused verification:** Cover every RV32I class, overflow and alignment edge cases, and each custom encoding including rejected masks.

The module uses the baseline [producer and edge-order rules](../module-architecture.md#4-baseline-protocol-and-event-ordering). Any future timing profile that changes these rules must document its own behavior and tests.
