# Results for conditional execution

`ConditionalResults` stores measurement results for conditional TCU output. It has
a separate delivery path from CPU feedback. Its latency is set by
`fast_result_latency` and can be longer or shorter than CPU result delivery.

## Connections

- **Input:** tagged measurement completions from `ControlLinks::fast_results`.
- **Output:** condition results and acknowledgments that return fast-feedback
  credits to the CPU.
- **Owner:** `TcuCycleModel`; history is committed at the end of a TCU transition.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Discriminator result mailbox"];
  owner [label="ConditionalResults"];
  state [label="history_\nProfile::history_depth"];
  output [label="Condition result and delivery acknowledgment"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Receiving and using a result

The TCU evaluates conditions first, then commits arriving measurement
results. `FastResultVisible` records the commit. A result committed at
100 ns cannot affect a condition evaluated at 100 ns; with a 20 ns TCU
period, it becomes usable at 120 ns.

Each target retains at most `history_depth` results. A new result evicts
the oldest when that limit is exceeded. Lookup requires the full
measurement reference captured by QAPPEND_IF, not the target's latest bit.

QREAD does not remove these results. Results for different targets can
arrive out of order, but results for one target must arrive in increasing
measurement-ID order.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `history_` | `vector<deque<Completion>>` | Bounded per-target history of exact measurement references and results. |
| `Profile::history_depth` | per-target bound | Evicts the oldest entry after an accepted new result. |

[C++ API](../api.md#feedbackhpp).

## Reset and errors

An absent or evicted required measurement reference raises a fault. Duplicate or out-of-order
results for the same target also fail. Old-epoch completions are discarded.
Session reset clears all history and pending delivery state.

When `fast_feedback` is disabled, the result path is inactive and conditional
output cannot obtain results through it.

## Implementation and tests

Source: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.fast`, `protocol.history`.

The tests check lookup by measurement reference, history eviction and duplicate results.
They also verify that a result arriving on an edge cannot affect that edge's
triggering decision.
