# Fast-Condition History

`FastHistory` stores measurement results for conditional TCU output. It has
its own delivery path and latency, so a result can reach the CPU before or after
it reaches the TCU. The fast path bypasses the CPU branch; the name does not
promise a shorter result-delivery latency.

## Connections

- **Input:** tagged measurement completions from `ControlLinks::fast_results`.
- **Output:** predicate results for launch preflight and delivered-token
  acknowledgments that return fast-feedback credits to the CPU.
- **Owner:** `TcuCycleModel`; history is committed at the end of a TCU transition.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Discriminator result mailbox"];
  owner [label="FastHistory"];
  state [label="history_\nProfile::history_depth"];
  output [label="Predicate result / delivered credit"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Receiving and using a result

The TCU first evaluates the due group's conditions using existing history.
It then commits eligible incoming completions. A `FastResultVisible` event marks
this commit; conditions can use the new result starting on the next TCU edge.

History is bounded per target. A new completion appends its full token and bit;
when `history_depth` is exceeded, the oldest entry is removed. Lookup requires
the exact token captured by QAPPEND_IF, not merely the latest bit for that qubit.

For example, if a result is committed at the 100 ns TCU edge, a group firing at
100 ns cannot use it. With a 20 ns period, a group at 120 ns can use it if the
entry is still retained.

CPU result consumption does not erase TCU history. Measurements on different
targets may complete out of order; results for one target must arrive in
increasing measurement-ID order.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `history_` | `vector<deque<Completion>>` | Bounded per-target history of exact measurement tokens/results. |
| `Profile::history_depth` | per-target bound | Evicts the oldest entry after an accepted new result. |

[C++ API](../api.md#feedbackhpp).

## Reset and errors

An absent or evicted required token raises a fault. Duplicate or out-of-order
results for the same target also fail. Old-epoch completions are discarded.
Session reset clears all history and pending delivery state.

When `fast_feedback` is disabled, the result path is inactive and conditional
output cannot obtain results through it.

## Implementation and tests

Source: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.fast`, `protocol.history`.

The tests check exact-token lookup, history eviction and duplicate results.
They also verify that a result arriving on an edge cannot affect that edge's
firing decision.
