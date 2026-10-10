# Quantum instruction adapter

`adapt_quantum()` translates a decoded quantum instruction and its register
operands into a `ControlOperation` for the CPU-side timing control.

## Connections

- **Input:** `rv32::Decoded`, instruction ID and captured operand values.
- **Output:** one `ControlOperation` for `TimingControl::execute()`.
- **Caller:** the CPU, while preparing or submitting a decoded control operation.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Decoded quantum instruction and operands"];
  owner [label="adapt_quantum"];
  state [label="rv32::Decoded\nControlOperation"];
  output [label="ControlOperation"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records and configuration"];
}
```

## Mapping an instruction

The `funct3` field selects a command payload in `ControlOperation`. The `cw` mode selects immediate
or register operands for the port and codeword. `wait` supplies a cycle
interval; `fmr` supplies a qubit index.

The CPU passes the resulting operation to timing control. An absent
optional result keeps the instruction blocked; a returned value lets
the CPU advance past that operation. A scalar instruction retires after
one accepted operation; a bundle retires after both are accepted.
The adapter itself neither queues events nor waits.

See [instruction encodings](../interfaces.md#quantum-instruction-encoding)
and [reserve-phase behavior](reserve-phase.md).

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `rv32::Decoded` | input record | Validated custom-0 encoding. |
| `ControlOperation` | output record | Instruction ID, operation kind and operands. |

[C++ API](../api.md#timing_controlhpp).

## Reset and errors

The adapter rejects non-quantum input. The decoder checks malformed custom
encodings before they reach it. A valid sync becomes a Synchronize operation,
which timing control stores at the current time point after validating the
neighbor connection. An unconnected target raises `UnsupportedSynchronization`.

The adapter has no retained state. The CPU and timing control clear their held
instruction state on reset.

## Implementation and tests

Source: [timing_control.cpp](../../src/timing_control.cpp) and [timing_control.hpp](../../include/qsbit/timing_control.hpp).

**CTest:** `control.instructions`, `core.isa_decode`, `systemc.use_cases`.

The tests check all codeword operand modes, interval bounds, FMR register
selection, reserved encodings and the synchronization fault.
