# Simulation timing and event ordering

The CPU, TCU and device models advance on clock edges and scheduled events.
This reference defines when messages arrive, which changes occur on each edge,
and when a run completes.

## Timing and implementation objects

A tick is one nanosecond of simulation time. CPU and TCU clocks may have
different periods and phases. The current time point is the logical TCU cycle
being prepared by the CPU. Its timing label identifies the corresponding events.

`TimingControl` collects the events for a time point in bounded storage, then
submits a `TimingEvents` request containing the time point and its events.
The request stays unchanged until acknowledgment.

| C++ owner or interface | Implementation responsibility |
| --- | --- |
| `ProgramImage` and `rv32` | Load programs and calculate instruction effects. |
| `ICpuCycleModel` and `MemoryModel` | Advance the selected CPU and service timed memory requests. |
| `TimingControl` and `MeasurementRegisters` | Prepare time points and maintain measurement result registers. |
| `TcuCycleModel` | Own timing and event queues, timer and execution flags. |
| `Core` and `SyncUnit` | Own one controller's components and its neighbor synchronization state. |
| `ControlElectronics` | Schedule output, acquisition and discrimination; check resource conflicts. |
| `IQuantumBackend` | Supply quantum-state evolution and measurement outcomes according to backend capabilities. |
| `Simulator` | Schedule calls, reset and same-tick device processing, and determine completion. |

Direct calls add no implied delay. Command, reply, memory and measurement
paths use bounded mailboxes with explicit arrival ticks. The
[implementation map](implementation.md#implementation-map) shows their connections.

## Reserve phase operations and progress

The model starts at time point zero without a pending timing entry.
`cw` prepares events there. A positive `wait` skips an untouched origin.
Each subsequent time point enters the timing queue, even if it has no events.

| Instruction | Completion rule | Effect |
| --- | --- | --- |
| `cw` | Events are validated and stored. | Adds events at the current time point; measurement increments the target's pending count. |
| `sync` | The connected target is validated and stored. | Adds a synchronization event at the current time point. |
| `wait` with zero interval | Immediate. | No changes. |
| `wait` with positive interval d | Pending events are enqueued and acknowledged. | Advances the time point by d TCU cycles. |
| `fmr` | Required enqueue completes and the target register's pending count reaches zero. | Copies the bit into a GPR. |
| Exit ECALL | Required enqueue completes and closure is published. | Halts the CPU; simulation continues until drain. |

`cw` may retire before queue insertion. Two codewords at time point 4 followed
by `wait.i 3` enqueue both events for cycle 4, then advance the time point to 7.
After `fmr` enqueues a point, a positive `wait` is required before another `cw`.

Only one enqueue request may await a reply. A blocked instruction retains its
identity and operands across retries, preventing duplicate requests.

Events exceeding the staging limit, per-port limits or measurement delivery
capacity raise `Capacity`. Each result path has `result_capacity` entries;
CPU delivery and TCU acknowledgment release their respective entries.

## Communication latency and TCU edge order

For publication tick p, let r0 be the first receiver edge **strictly after p**.
With configured latency N receiver edges, N >= 1:

```text
arrival = r0 + (N - 1) * receiver_period
```

This rule applies to enqueue requests, replies and feedback. The mailbox stores
the arrival tick in `eligible`. Queue capacity can delay consumption beyond
arrival, but the event deadline stays unchanged.

For CPU period 5 ns, TCU period 20 ns and one-edge communication latency:

| Tick | Earliest possible event |
| --- | --- |
| 20 ns | CPU publishes an enqueue request. |
| 40 ns | TCU inserts the time point and events and publishes its reply, if capacity and deadline permit. |
| 45 ns | CPU receives the reply. |

The requested time point must be later than 40 ns. Its events may trigger before
the CPU receives acknowledgment if the reply path is slower.

At each TCU rising edge:

1. Read existing queue state and arrived messages. If session reset applies,
   skip the ordinary transition.
2. Update synchronization state from arrived signals and countdown deadlines.
   Calculate the current logical cycle. If the timer is not paused and the existing head is due, match its
   complete manifest, check the existing execution flags, and validate
   the selected device events.
3. Validate the candidate time point's identity, mapping and deadline, then
   check capacity.
   Use queue occupancy from the **start of the edge**. A slot freed by triggering
   on this edge becomes available on the next edge.
4. Validate incoming fast results without changing the flags used in step 2.
   Any validation fault prevents this transition's triggering and enqueue commits.
5. Remove the triggered time point, insert the candidate if space permits,
   and emit the corresponding trace records.
   Update execution flags from incoming results for use on later edges.

At most one old point triggers and one new time point is enqueued per edge. A newly
enqueued time point cannot trigger on its enqueue edge. These steps run within one
TCU transition; they do not each consume a clock cycle.

## Start, deadlines and empty queues

Without synchronization pauses, effective epoch start S and period P place
logical cycle n at `S + n * P`. Each paused TCU edge delays subsequent logical
cycles by P without delaying physical device evolution. A pending cycle triggers
on its resume edge. Admission deadlines include elapsed pauses.
Initially S is `profile.start`; session reset computes a new S as described below.
The start edge is cycle zero. Start is configured independently of CPU progress,
so a blocked timing control cannot prevent the timer from starting.

Enqueue adds each timing interval to the last enqueued due cycle, even if the
queue became empty between time points. For intervals 4 and 3, the due cycles are
4 and 7. The second point must be enqueued before cycle 7; arrival does not
rebase it.

An empty timing queue does not stop the timer or cause an immediate fault.
The timing control can still submit a future point before its deadline. A point due
at cycle zero must be enqueued before start. Late arrival raises `LateAdmission`.

The time point's manifest lists exact event IDs. A missing or extra member
faults the complete time point. A port absent from the manifest remains idle.
An empty manifest is a valid wait-only point.

## Device batches and feedback

The device barrier waits for every CPU, memory and TCU transition due at tick t
to finish. It then processes all physical work assigned to t, including
zero-delay events just triggered by the TCU.

`TriggeredEvents` contains events selected at one timing label. At a simulation
timestamp, device processing includes all starts, ends, samples and ready results,
possibly from several timing labels. Output delays can place events from one
label at different timestamps.

Before changing quantum state, the model validates all device work at that timestamp.
It then:

1. Commits the preceding evolution interval with all previously active drives.
2. Executes pending operations, samples and collapses ending acquisitions, and ends old channel intervals.
3. Commits starting ideal gates and starts new pulse, acquisition and arm intervals.
4. Publishes results whose discriminator delay has completed.

Without an ending acquisition, operations remain pending until the batch limit,
state inspection or successful completion. See [backend execution](backends.md#backend-execution).

Every event has positive duration and reserves `[start, end)`. Adjacent
intervals may share an endpoint. Ideal gates change state at their start while
retaining the configured occupancy interval.

Overlapping pulse drives are evolved jointly. Drives active over [10,30) and
[20,40) produce three evolution intervals: [10,20), [20,30) with both drives,
and [30,40). Individual ports cannot advance a shared backend independently.

A measurement sample and a starting ideal gate on the same target at the same
tick are unsupported and fail before mutation. Resource or capability conflicts
also reject the batch. A numerical backend failure terminates the run
without rolling back numerical state.

For acquisition end E, discriminator arm A and delay L, the result is ready at
`max(E, A) + L`. An implicit arm uses acquisition start. L may be zero, but
feedback still follows the strictly later receiver-edge rule. CPU and TCU
consumers cannot see a result newly produced at their current edge.

## Closure and drain

The exit ECALL publishes an `EndOfStream` marker after enqueue acknowledgment.
The marker contains the last enqueued label; zero denotes an empty stream.
Its mailbox envelope supplies epoch and visibility timing.

Success requires all of the following:

- For every core, the CPU has halted, timing control is closed, and the TCU has
  received closure.
- Every core's timing and event queues are empty; shared physical events and
  scheduled readouts are empty.
- Every core's memory transactions and communication mailboxes have drained.
- Decoder requests, the reset slot, active jobs and incomplete measurement
  windows are empty.
- Every core's synchronization unit has completed its request, and the
  synchronization network has no unconsumed signals.
- Every core's CPU and enabled fast-feedback deliveries, including credit
  acknowledgments, have completed.

Measurement registers retain their bits after completion.
Watchdog expiry reports a failed run.

## Session reset

Reset takes precedence over ordinary work at its tick and starts a new epoch.
It clears every core's CPU, timing control, mailboxes, TCU queues, execution flags,
measurement registers and synchronization unit. It also resets `SyncNetwork`
and `DecoderSystem`, and clears shared device reservations and readouts.
Active and future device events receive reset-abort records. The backend is
initialized again with the configured qubit count and seed.

Each CPU returns to its loaded entry PC. Memory bytes and profiles are preserved.

The new TCU start is the first TCU edge at or after
`reset_tick + profile.start`. SystemC time never rewinds. Stale completions
cannot change the new epoch. Time, label and ID overflow raises a fault instead
of silently wrapping.

## Records and verification

[C++ interfaces](cpp-interfaces.md) define the actual records. `EnqueueReply`
acknowledges enqueue; `CodewordQueued` and `TimingPointEnqueued` are trace events.
The trace schema is documented in [file formats](interfaces.md#jsonl-trace).

The [module pages](modules/README.md) identify source files and registered tests.
The [testing guide](engineering-and-testing.md) explains how to run them.
