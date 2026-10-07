# Reserve phase implementation

`TimingControl` prepares codeword events at the current time point and requests
their insertion into the timing and event queues. The current time point
accumulates requested positive wait intervals independently of the running timer.

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

`cw` looks up the port and codeword, validates the resulting events and
stores them for the current time point. An acquisition increments the target
measurement register's pending count and reserves result delivery capacity.
Several codewords can contribute events to one time point.

A positive `wait` enqueues the pending time point, waits for acknowledgment,
then advances by the requested number of TCU cycles. `wait 0` enqueues the
current point with permission to wait for subsequent queue entries and opens
a new point without adding a requested interval. The TCU separates consecutive
zero-interval points by one logical cycle.

For example, two `cw` instructions at cycle 4 followed by `wait.i 3`
enqueue both events for cycle 4. The current time point becomes 7 after
acknowledgment. The TCU timer continues independently.

FMR and the exit ECALL wait for enqueue acknowledgment. FMR then waits for
the selected measurement register to become valid. The exit ECALL publishes
an `EndOfStream` message.
Only one enqueue request can be pending. A blocked instruction keeps the
same ID and operands across retries.

At startup, the untouched time point zero needs no queue entry. A positive
`wait` creates a time point that must later be enqueued even if no events
are added. See [instruction completion](../module-architecture.md#reserve-phase-operations-and-progress).

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `time_point_, last_enqueued_time_` | Accumulated wait intervals | Their difference is the next requested interval; the TCU resolves the due cycle. |
| `pending_events_, pending_point_, enqueued_` | Pending events and status | Retain events and track whether more may be added at this time point. |
| `wait_zero_pending_, allow_same_time_` | Zero-wait status | Retain a zero wait across enqueue retries and permit subsequent codewords without a positive interval. |
| `enqueue_request_` | Optional TimingEvents | Fixed request awaiting acknowledgment. |
| `held_` | Optional ControlOperation | Retains the blocked instruction and operands. |
| `last_label_, next_event_, closed_` | Identifiers and status | Allocate timing labels and event IDs and record completion. |

[C++ API](../api.md#timing_controlhpp).

## Reset and errors

An event set exceeding staging capacity, per-port width or per-port queue capacity
raises `Capacity`. Accepting a measurement when either delivery path has
`result_capacity` outstanding entries also raises `Capacity`.

Reset clears pending events, the enqueue request and held instruction, returns
the time point to zero and restarts identifiers in the new epoch.

## Implementation and tests

Source: [timing_control.cpp](../../src/timing_control.cpp) and [timing_control.hpp](../../include/qsbit/timing_control.hpp).

**CTest:** `protocol.timing`, `protocol.capacity`, `systemc.use_cases`.

Tests check stable instruction identity across retries, exactly one enqueue,
time point advancement after acknowledgment, bounded storage, and measurement
feedback through `wait 0` with delayed CPU submission.
