# Timing queue

The timing queue stores time points and their due cycles in
`TcuCycleModel::timing_`.

## Connections

- **Input:** a `TimingPoint` enqueued together with its associated events.
- **Output:** the next label, due cycle and manifest for the TCU triggering decision;
  queue occupancy for enqueue checks.
- **Owner:** `TcuCycleModel::timing_`, updated during the TCU edge transition.

## Keeping time across queue gaps

Each enqueue adds its interval to `last_due_` and stores the resulting
due cycle. The first interval is measured from logical cycle zero. Subsequent
zero-interval entries consume one cycle.

A time point with no events represents a wait. Queue gaps do not reset
`last_due_`. See [pause and deadline rules](../module-architecture.md#start-deadlines-and-empty-queues)
for cumulative intervals and empty-queue behavior.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Point::point` | `TimingPoint` | Epoch, label, interval, `UnderflowPolicy`, synchronization requests and exact event-ID manifest. |
| `Point::due` | logical TCU cycle | Cumulative due cycle calculated during enqueue. |
| `last_due_` | logical TCU cycle | Last enqueued due cycle, retained even when the FIFO empties. |

`UnderflowPolicy::Inherit` retains the preceding policy. `Strict` keeps the
timer running when the queue empties. `PauseWhenEmpty` permits the timer to
pause until new work arrives. Admission updates the permission for same-cycle
enqueue; triggering updates the policy used to pause the timer.

[C++ API](../api.md#tcuhpp).

## Reset and errors

Enqueue on or after the due tick raises `LateAdmission`, except at a cycle
frozen while waiting for work after `wait 0`. Missing, extra
or stale manifested events raise `ManifestMismatch` before triggering.
Reset clears the queue, cumulative due cycle and label sequence.

## Implementation and tests

Source: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.admission`, `control.empty`, `control.wait_zero`.

The tests check queue capacity before triggering and cumulative due times,
including strict deadlines, explicit pauses, synchronization overlap and reset.
