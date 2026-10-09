# ISA decoder and semantics

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
  owner [label="rv32::decode and evaluate"];
  state [label="Decoded\nEffect"];
  output [label="Decoded and Effect or typed fault"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records and configuration"];
}
```

## Decode and evaluate

`decode()` identifies RV32I and custom-0 instructions, checks fixed encoding
bits, and extracts registers and immediates.

The [VLIW CPU](cpu-cycle-model.md#dual-codeword-execution) decodes custom-1
bundles with `decode_cw_bundle()`.

`evaluate()` calculates arithmetic results, branch decisions, the next PC
and memory access parameters. The CPU commits those effects and issues
memory requests. RV32I arithmetic wraps at 32 bits.

Quantum instructions pass through
[`adapt_quantum()`](quantum-instruction-adapter.md) to produce a
`ControlOperation`. Both decoding paths run within the CPU edge.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Decoded` | value record | Operation, original word, register fields, immediate and register-use flags. |
| `Effect` | value record | Next PC, result, memory request parameters and branch outcome. |

[C++ API](../api.md#isahpp).

## Reset and errors

Invalid or unsupported encodings raise `IllegalInstruction`. The ISA library
retains no state to reset.

The CPU model intercepts the exit ECALL; other ECALLs and EBREAK produce distinct
traps. It delays a speculative instruction's fault until that instruction
becomes oldest, so a taken branch can discard a wrong-path fault.

## Implementation and tests

Source: [isa.cpp](../../src/isa.cpp) and [isa.hpp](../../include/qsbit/isa.hpp).

**CTest:** `core.isa_arithmetic`, `core.isa_control`, `core.isa_decode`.

The ISA tests check arithmetic results, branch and jump effects, and the
acceptance or rejection of individual instruction encodings.
