# Fast-Condition History

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Optional TCU-domain history plus its own result crossing; it does not share CPU result visibility.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Tagged discriminator completion and TCU clock edges. |
| Downstream outputs | Committed condition snapshot for launch preflight and fast-delivery credit release. |
| State owner and retained state | Per-target result history, predicate table, validity bit, crossing mailbox and epoch. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Discriminator result mailbox"];
  owner [label="FastHistory"];
  state [label="history_; Profile::history_depth"];
  output [label="Predicate result / delivered credit"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Completion becomes eligible after configured TCU receiver-edge latency; TCU edge commits it after that edge’s due actions sample the prior snapshot.

**Transition:** Update history for every completed measurement independently of CPU result consumption and later pending measurements. An always-true selector supports unconditional events. A conditional event reads the prior committed history snapshot; a false predicate creates an explicit cancellation.

**Time and visibility:** A completion arriving on the same edge as a due label affects only a later edge. Per-target issue order is required in the baseline optional profile; unrelated targets may complete out of order.

**Reset and errors:** Missing required history faults. Conditional measurement is unsupported in the baseline to avoid leaving a token pending after cancellation. Reset clears history and old deliveries.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `history_` | `vector<deque<Completion>>` | Bounded per-target history of exact measurement tokens/results. |
| `Profile::history_depth` | `per-target bound` | Evicts the oldest entry after an accepted new result. |

evaluate requires an exact token match and compares its bit with the predicate. commit rejects same-target duplicates/out-of-order IDs, appends a valid completion and enforces depth. TCU decisions evaluate the previously committed history; same-edge incoming results become usable on a later TCU edge. Reset clears all histories.

[Current C++ declarations](../api.md#feedbackhpp).

## Implementation and verification

FastHistory retains exact-token results per target. TcuCycleModel commits arriving results after that edge's firing decision.

- Implementation: [feedback.cpp](../../src/feedback.cpp) and [feedback.hpp](../../include/qsbit/feedback.hpp).

**CTest:** `control.fast`, `protocol.history`.

Checks exact-token lookup, eviction, duplicate results and exclusion of same-edge arrivals from firing decisions.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
