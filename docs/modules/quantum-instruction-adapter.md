# Quantum instruction adapter

`adapt_quantum()` translates a decoded quantum instruction and its register
operands into a `ControlOperation`. This keeps binary instruction fields out of
the CPU-side timing control.

## Connections

- **Input:** `rv32::Decoded`, instruction ID, captured operand values and the
  measurement handle used by QAPPEND_IF.
- **Output:** one `ControlOperation` for `TimingControl::execute()`.
- **Caller:** the CPU, when the quantum instruction is oldest and may issue its effect.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Authorized decoded operation"];
  owner [label="adapt_quantum"];
  state [label="rv32::Decoded\nControlOperation"];
  output [label="ControlOperation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records and configuration"];
}
```

## Mapping an instruction

The `funct3` field selects a `ControlKind`. QAPPEND supplies port and
codeword register values. QAPPEND_IF also reads the register named by
`rd` as its measurement handle; it does not write that register.

The CPU passes the resulting operation to timing control. An absent
optional result keeps the instruction blocked; a returned value lets
it retire. The adapter itself neither queues events nor waits.

See [instruction encodings](../interfaces.md#quantum-instruction-encoding)
and [reserve-phase behavior](reserve-phase.md).

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `rv32::Decoded` | input record | Validated custom-0 encoding. |
| `ControlOperation` | output record | Instruction ID, operation kind, operands, measurement handle and expected bit. |

[C++ API](../api.md#producerhpp).

## Reset and errors

The adapter rejects non-quantum input. The decoder checks malformed custom
encodings before they reach it. A valid QSYNC becomes a Synchronize operation,
which the timing control rejects with `UnsupportedSynchronization`.

The adapter has no retained state. The CPU and timing control clear their held
instruction state on reset.

## Implementation and tests

Source: [producer.cpp](../../src/producer.cpp) and [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `core.isa_decode`, `systemc.use_cases`.

The tests check extension encodings and execute QAPPEND, QADVANCE, QFLUSH,
QREAD, QEND and conditional events. A QSYNC case checks the unsupported
synchronization fault.
