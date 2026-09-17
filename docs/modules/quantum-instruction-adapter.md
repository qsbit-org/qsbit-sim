# Quantum Instruction Adapter

`adapt_quantum()` translates a decoded quantum instruction and its register
operands into a `ProducerOperation`. This keeps binary instruction fields out of
the timeline producer.

## Connections

- **Input:** `rv32::Decoded`, instruction ID, captured operand values and the
  predicate handle used by QAPPEND_IF.
- **Output:** one `ProducerOperation` for `TimelineProducer::execute()`.
- **Caller:** the CPU, when the quantum instruction is oldest and may issue its effect.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Authorized decoded operation"];
  owner [label="adapt_quantum"];
  state [label="rv32::Decoded\nProducerOperation"];
  output [label="ProducerOperation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records / configuration"];
}
```

## Mapping an instruction

The instruction's `funct3` field selects Append, Advance, Flush, ReadResult,
End, ConditionalAppend or Synchronize. For example, QAPPEND captures the port
and codeword register values. QAPPEND_IF also captures the register named by
`rd` as a predicate-handle source; it does not write that register.

The adapter performs no queue insertion or waiting. The producer validates the
requested mapping and returns an optional result. An absent result keeps the CPU
instruction blocked; a present result allows it to retire.

See the [instruction reference](../interfaces.md#quantum-instruction-encoding)
for field encodings and the [producer](timeline-reservation-manager.md) for
completion rules. Conversion is a direct C++ call within the CPU edge and adds
no separate cycle.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `rv32::Decoded` | input record | Validated custom-0 encoding. |
| `ProducerOperation` | output record | Instruction ID, operation kind, operands, predicate handle and expected bit. |

[C++ API](../api.md#producerhpp).

## Reset and errors

The adapter rejects non-quantum input. The decoder checks malformed custom
encodings before they reach it. A valid QSYNC becomes a Synchronize operation,
which the producer rejects with `UnsupportedSynchronization`.

The adapter has no retained state. The CPU and producer clear their held
instruction state on reset.

## Implementation and tests

Source: [producer.cpp](../../src/producer.cpp) and [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `core.isa_decode`, `systemc.use_cases`.

The tests check extension encodings and execute APPEND, ADVANCE, FLUSH,
READ_RESULT, END and conditional actions. A QSYNC case checks the unsupported
synchronization fault.
