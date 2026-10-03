# Timing controller and event trigger

The TCU timer determines when enqueued time points trigger. On each TCU rising edge,
it checks whether the timing-queue head is due and uses its label to select the
corresponding port events.

## Connections

- **Input:** current tick, configured start and TCU period, and the existing
  timing-queue head.
- **Output:** a validated `TriggeredEvents` and `TimingPointTriggered` record when a time point is due.
- **Scheduling:** `TcuCycleModel::step()` runs on each TCU rising edge.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TCU clock and timing head"];
  owner [label="TcuCycleModel"];
  state [label="start_\ncycle (local variable)\ntiming_.front().due"];
  output [label="TcuOutput and TimingPointTriggered"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="state and derived values"];
}
```

## From logical cycle to global tick

Without synchronization pauses, epoch start S and TCU period P place logical
cycle n at `S + n * P`. Each paused edge adds P to subsequent trigger ticks.
Before S, time points can enter the queues but cannot trigger.

With S = 100 ns and P = 20 ns, cycle 4 occurs at 180 ns. A time point
enqueued at 160 ns can trigger then; enqueue at 180 ns is too late.

The TCU checks the head's event IDs, evaluates conditions and validates
selected events before removing anything from the queues. New results
are committed after condition evaluation and become usable on later edges.

The timer continues through empty queues and CPU stalls. It does not
shift deadlines to accommodate a late request.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `start_` | global tick | Start of the current epoch timeline. |
| `paused_ticks_` | elapsed simulation ticks | Total delay from paused TCU edges. |
| Local variable `cycle` | derived logical cycle | Calculated from current tick, epoch start, period and elapsed pauses. |
| `timing_.front().due` | next due cycle | Selects the time point already queued when the edge begins. |
| `closed_` | closure state | Input closed after validated EndOfStream; queues may still drain. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

A queued point that misses its due edge or a time point enqueued too late raises
`LateAdmission`. Time arithmetic is checked for overflow.

Reset clears the TCU queues and execution flags. The new start is the first TCU edge at
or after `reset_tick + profile.start`. SystemC time continues from the reset tick.
The [synchronization unit](synchronization.md) pauses and resumes
the timer. Reset clears its pending requests and the accumulated pause duration.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.empty`, `systemc.tcu_trace`.

`control.empty` checks cumulative trigger ticks across empty gaps and
late-enqueue rejection. `systemc.tcu_trace` checks completion and label output
in a clocked harness.
