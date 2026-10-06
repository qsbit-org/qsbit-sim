# Execution flags

`ExecutionFlags` updates each qubit's flags from completed measurements.
The TCU checks the selected flag when an operation is triggered.

## Connections

- **Input:** measurement completions from `ControlLinks::fast_results`.
- **Output:** the selected flag and a delivery acknowledgment.
- **Owner:** `TcuCycleModel` commits results after checking due events.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Measurement result"];
  owner [label="ExecutionFlags"];
  state [label="Per-qubit execution flags"];
  output [label="Go or no-go at event trigger"];
  input -> owner; owner -> output; state -> owner [style=dashed];
}
```

## Flag updates

| Flag | Value |
| --- | --- |
| `always` | One. |
| `last_one` | Latest completed measurement bit. |
| `last_zero` | Inverse of the latest completed measurement bit. |
| `equal` | One when the latest two completed measurements agree. |

Reset clears the three conditional flags. The first result sets
`last_one` and `last_zero`; `equal` remains zero until a second result arrives.

A result committed at 100 ns cannot affect an operation triggered at 100 ns.
With a 20 ns TCU period, the updated flags become usable at 120 ns.
`ExecutionFlagsUpdated` records the result commit.

Pending measurements do not invalidate these flags. `fmr` neither changes
nor consumes them.

## Objects and state

| Member | Role |
| --- | --- |
| `registers_` | Per-qubit conditional flags and the last committed measurement ID. |
| `TcuCycleModel::execution_flags_` | TCU-owned flag storage. |

[C++ API](../api.md#feedbackhpp).

## Reset and errors

Reset clears all conditional flags and measurement IDs. Old-epoch results
are discarded. Duplicate or out-of-order results for one qubit raise
`Protocol`. Selecting a conditional codeword with `fast_feedback` disabled
raises `UnsupportedCapability`.

## Implementation and tests

Source: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.fast`, `protocol.flags`, `systemc.use_cases`.

Tests cover all four flags, target isolation, reset, consecutive results
and result arrival on the trigger edge.
