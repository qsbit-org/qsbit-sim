# TCU Timer and Label Broadcaster

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

One TCU rising-edge process owns deterministic time and invokes queue matching and launch preflight as logical substages.

| Direction | Contract |
| --- | --- |
| Upstream inputs | TCU clock, configured start tick, old timing-queue head. |
| Downstream outputs | Reached label to every event queue, logical-cycle and label-firing trace. |
| State owner and retained state | Stored start tick and queued due cycles; current logical cycle and running state are derived per call. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TCU clock and timing head"];
  owner [label="TcuCycleModel"];
  state [label="start_; cycle (local variable); timing_.front().due"];
  output [label="TcuOutput / LabelFired"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Wake on TCU rising edge; an external configured start enables it independently of CPU progress.

**Transition:** On configured start edge S, set `T_D=0`. With TCU period P, edge `S+nP` has `T_D=n`. If a previously admitted timing-queue head is due at n, broadcast its label and check the whole expected event group before firing. Logical time continues through empty-queue gaps. No pause/resume path exists in the baseline.

**Time and visibility:** No label is fired merely because an sc_event occurs; only the defined TCU edge controls this timer. A new group admitted on the same edge cannot fire.

**Reset and errors:** Baseline rejects pause and sync requests. Reset stops the timer and clears epoch state without rewinding SystemC time; overflow faults.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `start_` | `global tick` | Start of the current epoch timeline. |
| `cycle (local variable)` | `derived logical cycle` | Calculated from current tick and period; not a stored running counter. |
| `timing_.front().due` | `next due cycle` | Selects the old group to fire. |
| `closed_` | `closure state` | Input closed after validated EndOfStream; queues may still drain. |

Before start, queues may prefill but do not fire. At tick start+n*period, logical cycle is n; a due old head triggers manifest/condition/preflight checks. Candidate admission is evaluated against old occupancy, then successful firing/admission commit. Incoming fast results commit after firing decisions. Reset clears queues/history and schedules a new start on the TCU grid.

[Current C++ declarations](../api.md#tcuhpp).

## Implementation and verification

TcuCycleModel computes logical cycles from the epoch start and broadcasts the due label. Empty gaps preserve cumulative time.

- Implementation: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.empty`, `systemc.tcu_trace`.

`control.empty` asserts cumulative launch ticks across empty gaps and late-admission rejection. `systemc.tcu_trace` checks that its clocked harness drains and writes the label trace.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
