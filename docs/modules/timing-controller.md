# Timing controller and event trigger

The TCU timer determines when enqueued time points trigger. On each TCU rising edge,
it checks whether the timing-queue head is due and uses its label to select the
corresponding port events.

## Connections

- **Input:** current tick, configured start and TCU period, and the existing
  timing-queue head.
- **Output:** a validated `TriggeredEvents` and `TimingPointTriggered` record when a time point is due.
- **Scheduling:** `TcuCycleModel::step()` runs on each TCU rising edge.

## From logical cycle to global tick

`step()` converts the physical tick to a logical cycle using the epoch start,
TCU period and accumulated pause duration. The [timing reference](../module-architecture.md#start-deadlines-and-empty-queues)
defines the conversion and deadline rules.

`prepare_trigger()` and `prepare_admission()` validate work against the state
at the start of the edge. `meets_deadline()` checks the admission deadline.
`step()` validates incoming results and pause-duration arithmetic before
committing queue changes, results and pause state.

Admission and triggering update separate underflow policies. `pause_` combines
instruction-supply and synchronization requests; `paused_ticks_` advances once
per paused edge, even when both apply.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `start_` | global tick | Start of the current epoch timeline. |
| `paused_ticks_` | elapsed simulation ticks | Total delay from paused TCU edges. |
| Local variable `cycle` | derived logical cycle | Calculated from current tick, epoch start, period and elapsed pauses. |
| `timing_.front().due` | next due cycle | Selects the time point already queued when the edge begins. |
| `closed_` | closure state | Input closed after validated EndOfStream; queues may still drain. |
| `admitted_policy_` | `UnderflowPolicy` | Policy after the last admitted point; controls admission of subsequent zero intervals. |
| `triggered_policy_` | `UnderflowPolicy` | Policy after the last triggered point; controls whether an empty queue pauses the timer. |
| `pause_` | `PauseState` | Records instruction-supply and synchronization pauses separately; either pauses the timer. |

[C++ API](../api.md#tcuhpp).

## Reset and errors

A queued point that misses its due edge or a time point enqueued too late raises
`LateAdmission`. Time arithmetic is checked for overflow.

Reset clears the TCU queues, execution flags, pending synchronization requests
and accumulated pause duration.
The new start follows the [session reset rule](../module-architecture.md#session-reset).

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.empty`, `control.wait_zero`, `control.atomic`, `systemc.tcu_trace`.

`control.empty` checks cumulative trigger ticks across empty gaps and
late-enqueue rejection. `systemc.tcu_trace` checks completion and label output
in a clocked harness. `control.wait_zero` checks admission before a zero wait
triggers, delayed admission, overlapping pause reasons, return to strict
deadlines, closure and reset. `control.atomic` checks that rejected requests
and results leave queued work and execution flags unchanged.
