# Measurement result registers

`MeasurementRegisters` stores one result bit and a pending measurement
count per qubit. `FMR` reads the bit when the count is zero.

## Connections

- **Input:** accepted measurements and CPU result deliveries.
- **Output:** a result bit or an incomplete read.
- **Owner:** `TimingControl` reserves measurements and delivers results before the CPU step.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Accepted measurement and result"];
  owner [label="MeasurementRegisters"];
  state [label="Per-qubit bit and pending count"];
  output [label="FMR result"];
  input -> owner; owner -> output; state -> owner [style=dashed];
}
```

## Reading a result

Accepting a measurement increments the target register's pending count.
Delivery updates the bit and decrements the count. Results for one qubit
must arrive in measurement order; different qubits can complete independently.

`FMR` enqueues pending events and waits until the selected register has no
pending measurements. Reading leaves its value unchanged. If two measurements
of a qubit are pending, the read waits for both and returns the second result.

## Objects and state

| Member | Role |
| --- | --- |
| `registers_` | Result bit and pending count for each qubit. |
| `pending_` | Measurement references awaiting CPU delivery. |
| `fast_pending_` | Measurement references awaiting TCU delivery acknowledgment. |
| `next_measurement_` | Next measurement ID in the epoch. |

Each delivery path holds at most `result_capacity` outstanding measurements.
CPU delivery releases its entry; TCU acknowledgment releases the fast entry.
Reading a register does not release delivery capacity.

[C++ API](../api.md#feedbackhpp).

## Reset and errors

Reset clears register values, pending counts and delivery records.
Old-epoch results are discarded. Duplicate results, mismatched references
and out-of-order results for the same qubit raise faults.
An out-of-range register index raises `InvalidOperand`.

## Implementation and tests

Source: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.registers`, `protocol.readout`, `systemc.use_cases`.

Tests cover pending counts, repeated reads, consecutive measurements, reset,
invalid register indices and independent delivery acknowledgments.
