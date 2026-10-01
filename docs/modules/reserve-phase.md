# Reserve phase implementation

`TimingControl` prepares codeword events at the current time point and requests
their insertion into the timing and event queues. The current time point is a
planned TCU cycle, independent of the running timer.

## Connections

- **Input:** ordered `ControlOperation` calls, enqueue replies, measurement
  completions and returned feedback credits.
- **Output:** instruction results, `TimingEvents` enqueue requests and
  `EndOfStream` through `ControlLinks`.
- **Scheduling:** `Simulator::cpu_edge()` receives replies before the CPU
  attempts its next control instruction.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="ControlOperation"];
  owner [label="TimingControl"];
  state [label="time_point_, last_enqueued_time_\npending_events_, enqueue_request_"];
  output [label="TimingEvents and instruction result"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state"];
}
```

## Preparing and enqueueing events

QAPPEND resolves a port and codeword, validates its events and adds them to
bounded pending storage. Acquisition also reserves a measurement result slot.
The instruction can retire before enqueue, allowing several instructions to
prepare events for the same time point.

Positive QADVANCE submits the pending timing point and events, waits for the
matching `EnqueueReply`, then advances the time point by the requested TCU
cycles. QADVANCE(0) changes nothing. QFLUSH submits without advancing;
further QAPPEND instructions require a positive QADVANCE first.

For example, two QAPPEND instructions at time point 4 followed by QADVANCE(3)
enqueue both events for cycle 4. The current time point becomes 7 after the
reply arrives. Neither operation changes the running TCU timer.

QREAD waits for any required enqueue reply and then the requested measurement.
QEND waits for any required enqueue reply and publishes closure. At most one
enqueue request can be outstanding. A blocked instruction retains its ID and
operands across retries.

The untouched origin needs no enqueue request. A positive QADVANCE creates a
time point that is subsequently enqueued even if it has no events.
The [timing contract](../module-architecture.md#reserve-phase-operations-and-progress)
specifies all completion cases.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `time_point_, last_enqueued_time_` | Logical TCU cycles | Planned cycle and last acknowledged due cycle; their difference is the next interval. |
| `pending_events_, open_, flushed_` | Pending events and status | Retain events and track whether more may be added at this time point. |
| `enqueue_request_` | Optional TimingEvents | Fixed request awaiting acknowledgment. |
| `held_` | Optional ControlOperation | Retains the blocked instruction and operands. |
| `last_label_, next_event_, closed_` | Identifiers and status | Allocate timing labels and event IDs and record completion. |

[C++ API](../api.md#producerhpp).

## Reset and errors

An event set exceeding staging capacity, per-port width or total queue capacity
fails immediately. Exhausted CPU result slots also fail: only a later QREAD
can release them, so stalling the current instruction would prevent progress.
The model can wait for fast-feedback credits released by earlier measurements.

Reset clears pending events, the enqueue request and held instruction, returns
the time point to zero and restarts identifiers in the new epoch.

## Implementation and tests

Source: [producer.cpp](../../src/producer.cpp) and [producer.hpp](../../include/qsbit/producer.hpp).

**CTest:** `protocol.producer`, `protocol.capacity`, `systemc.use_cases`.

Tests check stable instruction identity across retries, exactly one enqueue,
time-point advancement after acknowledgment, bounded storage, and multiple
QAPPEND instructions separated by QADVANCE(0) at the same time point.
