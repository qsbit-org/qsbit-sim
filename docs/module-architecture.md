# Simulation timing and event ordering

This reference specifies the executable model's communication and transition
rules. The [controller architecture](high-level-design.md) describes reserve and
trigger phases; this page defines the implementation choices needed to reproduce
the simulator's timing.

## Timing and implementation objects

A tick is one nanosecond of simulation time. CPU and TCU clocks may have
different periods and phases. The current time point is the logical TCU cycle
being prepared by the CPU. Its timing label identifies the corresponding events.

`TimingControl` collects the events for a time point in bounded storage, then
submits them for queue insertion. The request remains fixed until acknowledged.
`TimingEvents` is the C++ record containing that timing point and its events;
it does not introduce another architectural unit.

| C++ owner or interface | Implementation responsibility |
| --- | --- |
| `ProgramImage` and `rv32` | Load programs and calculate instruction effects. |
| `CpuCycleModel` and `MemoryModel` | Advance the CPU and service timed memory requests. |
| `TimingControl` and `MeasurementResults` | Prepare timing points and track individual measurements. |
| `TcuCycleModel` | Own timing and event queues, timer and conditional results. |
| `ControlElectronics` | Schedule output, acquisition and discrimination; check resource conflicts. |
| `IQuantumBackend` | Calculate quantum evolution and measurement outcomes. |
| `Simulator` | Schedule calls, reset and same-tick device processing, and determine completion. |

Direct calls add no implied delay. Command, reply, memory and measurement
paths use bounded mailboxes with explicit arrival ticks. The
[implementation map](implementation.md#implementation-map) shows their connections.

## Reserve phase operations and progress

The model starts at time point zero without a pending timing entry.
QAPPEND prepares events there. A positive QADVANCE skips an untouched origin
without enqueueing it. After positive QADVANCE creates a new time point, that
point must be enqueued even if no QAPPEND adds events.

| Instruction | Completion rule | Effect |
| --- | --- | --- |
| QAPPEND | Events are validated and stored; acquisition has a result slot. | Adds events at the current time point and returns a measurement handle, or zero for other events. |
| QADVANCE(0) | Immediate. | No changes, including after QFLUSH. |
| QADVANCE(d), d > 0 | Any pending timing point and events are enqueued and acknowledged. | Advances the time point by d and permits further codeword events. |
| QFLUSH | Any pending timing point and events are enqueued and acknowledged. | Keeps the time point but prevents further QAPPEND there. Repetition has no further effect. |
| QREAD | Required enqueue completes and the requested CPU result arrives. | Consumes the result slot and returns its bit. |
| QEND | Required enqueue completes and closure is published. | Stops instruction production; simulation continues until drain. |

QFLUSH at the untouched origin completes locally. Positive QADVANCE from that
origin or an already flushed point requires no enqueue reply.

QAPPEND may retire before queue insertion. Two QAPPEND instructions at time
point 4 followed by QADVANCE(3) enqueue both events for cycle 4, then advance
the time point to 7 after acknowledgment.

Only one enqueue request may await a reply. A blocked instruction retains its
identity and operands across retries, preventing duplicate requests.

An event set exceeding total capacity faults immediately. Exhausted CPU result
slots also fault because only a later QREAD can free them. The model waits only
for capacity that earlier work can release, such as feedback delivery credits.

## Communication latency and TCU edge order

For publication tick p, let r0 be the first receiver edge **strictly after p**.
With configured latency N receiver edges, N >= 1:

```text
arrival = r0 + (N - 1) * receiver_period
```

This rule applies to enqueue requests, replies and feedback. The mailbox stores
the arrival tick in its compatibility field `eligible`. Arrival permits
reception; queue capacity can delay consumption without moving the deadline.

For CPU period 5 ns, TCU period 20 ns and one-edge communication latency:

| Tick | Earliest possible event |
| --- | --- |
| 20 ns | CPU publishes an enqueue request. |
| 40 ns | TCU inserts the timing point and events and publishes its reply, if capacity and deadline permit. |
| 45 ns | CPU receives the reply. |

The requested time point must be later than 40 ns. Its events may trigger before
the CPU receives acknowledgment if the reply path is slower.

At each TCU rising edge:

1. Read existing queue state and arrived messages. If session reset applies,
   skip the ordinary transition.
2. Calculate the current logical cycle. If the existing head is due, match its
   complete manifest, evaluate conditions using existing history, and preflight
   the selected device actions.
3. Validate the candidate timing point's identity, mapping, deadline and capacity.
   Use queue occupancy from the **start of the edge**. A slot freed by event triggering
   on this edge becomes available on the next edge.
4. Validate incoming fast results without changing the history used in step 2.
   Any validation fault prevents this transition's event triggering and enqueue commits.
5. Remove the triggered timing point, insert the enqueued timing point, and emit their results.
   Commit incoming conditional results for use on later edges.

At most one old point triggers and one new timing point is enqueued per edge. A newly
enqueued timing point cannot trigger on its enqueue edge. These steps run within one
TCU transition; they do not each consume a clock cycle.

## Start, deadlines and empty queues

With effective epoch start S and period P, logical cycle n is due at `S + n * P`.
Initially S is `profile.start`; session reset computes a new S as described below.
The start edge is cycle zero. Start is configured independently of CPU progress,
so a blocked timing control cannot prevent the timer from starting.

Enqueue adds each timing interval to the last enqueued due cycle, even if the
queue became empty between timing points. For intervals 4 and 3, the due cycles are
4 and 7. The second point must be enqueued before cycle 7; arrival does not
rebase it.

An empty timing queue does not stop the timer or cause an immediate fault.
The timing control can still submit a future point before its deadline. A point due
at cycle zero must be enqueued before start. Late arrival raises `LateAdmission`.

The timing point's manifest lists exact event IDs. A missing or extra member
faults the complete timing point. A port absent from the manifest remains idle.
An empty manifest is a valid wait-only point.

## Device batches and feedback

The device barrier waits for every CPU, memory and TCU transition due at tick t
to finish. It then processes all physical work assigned to t, including
zero-delay actions just launched by the TCU.

`TriggeredEvents` contains events selected at one timing label. At a simulation
timestamp, device processing includes all starts, ends, samples and ready results,
possibly from several timing labels. Output delays can place events from one
label at different timestamps.

Before changing quantum state, the model validates all device work at that timestamp.
It then:

1. Evolves state once over the preceding interval under all previously active drives.
2. Samples and collapses ending acquisitions, and ends old channel intervals.
3. Applies starting ideal gates and starts new pulse, acquisition and arm intervals.
4. Publishes results whose discriminator delay has completed.

Every action has positive duration and reserves `[start, end)`. Adjacent
intervals may share an endpoint. Ideal gates change state at their start while
retaining the configured occupancy interval.

Overlapping pulse drives are evolved jointly. Drives active over [10,30) and
[20,40) produce three evolution intervals: [10,20), [20,30) with both drives,
and [30,40). Individual ports cannot advance a shared backend independently.

A measurement sample and a starting ideal gate on the same target at the same
tick are unsupported and fail before mutation. Resource or capability conflicts
also reject the batch. A numerical backend failure terminates the run without
requiring rollback.

For acquisition end E, discriminator arm A and delay L, the result is ready at
`max(E, A) + L`. An implicit arm uses acquisition start. L may be zero, but
feedback still follows the strictly later receiver-edge rule. CPU and TCU
consumers cannot see a result newly produced at their current edge.

## Closure and drain

END publishes an ordered `EndOfStream` marker after any required flush reply.
The marker contains the last enqueued label; zero denotes an empty stream.
Its mailbox envelope supplies epoch and visibility timing.

Success requires all of the following:

- The CPU has halted, the timing control is closed, and the TCU has received closure.
- Timing and event queues, physical actions and scheduled readouts are empty.
- Memory transactions and communication mailboxes have drained.
- All CPU and enabled fast-feedback deliveries, including credit acknowledgments,
  have completed.

Unread Visible CPU slots may remain as final state. They do not count as pending
deliveries. Watchdog expiry reports a failed run rather than successful closure.

## Session reset

Reset takes precedence over ordinary work at its tick and starts a new epoch.
It clears CPU and timing control state, mailboxes, TCU queues and history, device
reservations, readouts and result slots. Active actions receive reset-abort
records. The backend is initialized again with the configured qubit count and seed.

The CPU returns to the loaded entry PC. Memory bytes and the simulation profile
are preserved. Result-slot generation counters survive reset, so stale handles
cannot become valid through slot reuse.

The new TCU start is the first TCU edge at or after
`reset_tick + profile.start`. SystemC time never rewinds. Stale completions
cannot change the new epoch. Time, label and ID overflow raises a fault instead
of silently wrapping.

This is a complete simulation-session reset. A controller-only reset that
preserves qubit evolution would require a separate contract.

## Records and verification

[C++ interfaces](cpp-interfaces.md) define the actual records. `EnqueueReply`
acknowledges enqueue; `ProducerAccepted` and `GroupAdmitted` are trace events.
The trace schema is documented in [file formats](interfaces.md#jsonl-trace).

The [module pages](modules/README.md) identify source files and registered tests.
The [testing guide](engineering-and-testing.md) explains how to run them.
