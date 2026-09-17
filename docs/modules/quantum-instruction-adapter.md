# Quantum Instruction Adapter

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Pure mapping called from the authorized CPU commit path. It does not introduce its own SystemC thread.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Decoded extension effect, immutable operand snapshot, target configuration and active epoch. |
| Downstream outputs | One semantic producer operation or typed unsupported error; result reads consult the CPU-domain scoreboard. |
| State owner and retained state | No independently clocked state; pending instruction identity remains owned by the CPU. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Authorized decoded operation"];
  owner [label="adapt_quantum"];
  state [label="rv32::Decoded; ProducerOperation"];
  output [label="ProducerOperation"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="value records / configuration"];
}
```

## Behavior

**Activation:** Call only for the oldest non-speculative instruction when the CPU cycle model authorizes external publication.

**Transition:** Map codeword instructions to APPEND(port, codeword), waits to ADVANCE(interval), explicit sealing to FLUSH, result reads to READ_RESULT(token), and producer closure to END. These semantic operations map to the v1 custom-0 opcodes in ADR 0001. Use the configured port action map without assuming a codeword names a gate.

**Time and visibility:** Producer acceptance and TCU admission are distinct; the adapter returns whichever completion the semantic operation requires. It never advances T_D by sleeping a host process.

**Reset and errors:** Reject unsupported operation, port or operand before irreversible acceptance. No mutable state to clear on reset; stale-epoch requests are rejected.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `rv32::Decoded` | `input record` | Validated custom-0 encoding. |
| `ProducerOperation` | `output record` | Instruction ID, operation kind, operands, predicate handle and expected bit. |

The CPU calls adapt_quantum only for its oldest quantum instruction. funct3 selects Append, Advance, Flush, ReadResult, End, ConditionalAppend or Synchronize. Captured register values become operation operands. The adapter retains no state; TimelineProducer keeps a blocked operation identity. Synchronize reaches the explicit unsupported fault.

[Current C++ declarations](../api.md#producerhpp).

## Implementation and verification

adapt_quantum maps decoded custom-0 instructions and captured register operands into ProducerOperation values.

- Implementation: [producer.cpp](../../src/producer.cpp) and [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `core.isa_decode`, `systemc.use_cases`.

Checks extension encoding validation and execution of APPEND, ADVANCE, FLUSH, READ, END and conditional actions; QSYNC rejects.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
