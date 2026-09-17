# ISA Decoder and Semantics

The ISA library decodes instruction words and calculates their architectural
effects. It specifies the result of an instruction; the CPU model decides when
that result becomes visible.

## Connections

- **Input:** a 32-bit instruction word for `rv32::decode()`; a decoded instruction,
  PC and operand values for `rv32::evaluate()`.
- **Output:** `rv32::Decoded` and `rv32::Effect` values, or a typed fault.
- **Caller:** the CPU model's decode and execute stages.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Instruction word and operands"];
  owner [label="rv32::decode / evaluate"];
  state [label="Decoded\nEffect"];
  output [label="Decoded / Effect or typed fault"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records / configuration"];
}
```

## Decode and evaluate

`decode()` identifies RV32I and the version 1 custom-0 instructions. It extracts
register fields and immediates, checks fixed encoding bits, and records which
register operands the CPU must read.

`evaluate()` calculates results such as arithmetic values, branch decisions,
next PC and memory access parameters. It neither updates registers nor accesses
memory. RV32I arithmetic uses 32-bit wraparound; time and protocol IDs use checked
arithmetic elsewhere in the simulator.

The CPU sends decoded quantum instructions through
[`adapt_quantum()`](quantum-instruction-adapter.md). That path creates a producer
operation instead of executing a quantum effect inside the ISA library.
Both functions are synchronous C++ calls and add no simulated delay.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Decoded` | value record | Operation, original word, register fields, immediate and register-use flags. |
| `Effect` | value record | Next PC, result, memory request parameters and branch outcome. |

[C++ API](../api.md#isahpp).

## Reset and errors

Invalid or unsupported encodings raise `IllegalInstruction`. ECALL and
EBREAK produce distinct traps. The CPU delays a speculative instruction's fault
until that instruction becomes oldest, so a taken branch can discard a wrong-path
fault. The ISA library retains no state to reset.

## Implementation and tests

Source: [isa.cpp](../../src/isa.cpp) and [isa.hpp](../../include/qsbit/isa.hpp).

**CTest:** `core.isa_arithmetic`, `core.isa_control`, `core.isa_decode`.

The ISA tests check arithmetic results, branch and jump effects, and the
acceptance or rejection of individual instruction encodings.
