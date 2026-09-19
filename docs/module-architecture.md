# Control protocol and event ordering

This reference defines how the CPU producer, TCU and device runtime exchange
work. Use it when changing a timing rule or implementing an adapter. For an
introduction to the data path, start with the [architecture overview](high-level-design.md).

## Timing terms used below

[Simulation time and execution](simulation-model.md) introduces the scheduler,
processes and communication paths.

A **tick** is one nanosecond of simulation time. CPU and TCU clocks can have
different periods and phases on that grid.

The **producer cursor** is the logical TCU cycle the CPU is currently preparing.
An **open group** holds actions for that cycle. **Sealing** fixes the group's
label, interval and members so it can cross to the TCU.

**Admission** inserts a complete group into the TCU queues. **Firing** releases
that group when its planned cycle is reached. The label identifies the group;
it is not a timestamp. See the [glossary](glossary.md) for other terms.

## Module map

The [architecture diagram](architecture.md) and [module reference](modules/README.md)
show the connections and each module's local behavior.

| Owner or library | Work |
| --- | --- |
| `ProgramImage` and `rv32` | Load programs and define instruction effects. |
| `CpuCycleModel` and `MemoryModel` | Advance the CPU and timed memory accesses. |
| `TimelineProducer` and `Scoreboard` | Prepare groups and track measurement handles. |
| `TcuCycleModel` | Own the timing queue, per-port queues, timer and fast history. |
| `DeviceRuntime` | Reserve resources and process physical actions and readout. |
| `IQuantumBackend` | Calculate quantum evolution and measurements. |
| `Simulator` | Schedule model calls and coordinate reset, device boundaries and stop. |

Direct C++ calls add no implied clock delay, including calls between owners.
The command, reply, memory and measurement-delivery paths use bounded mailboxes
with explicit eligibility ticks. Device preflight and launch acceptance are
direct calls from the SystemC adapter.

## Producer operations and progress

The producer starts at cursor zero with no open group. The first APPEND opens
a group there. A positive ADVANCE can skip the untouched origin without submitting
an empty group. Once ADVANCE opens a new group, sealing it submits a timing entry
even if it has no events.

| Operation | When it completes | State change |
| --- | --- | --- |
| APPEND | After action validation, staging and any measurement-slot reservation. | Adds actions at the cursor and returns a handle for acquisition, otherwise zero. |
| ADVANCE(0) | Immediately in the producer call. | None; it neither seals nor reopens a flushed group. |
| ADVANCE(d), d > 0 | After any open group is admitted and its reply reaches the CPU. | Moves the cursor by d and opens a group there. |
| FLUSH | After any open group is admitted and its reply reaches the CPU. | Keeps the cursor, closes the group to further APPENDs. Repeated FLUSH has no further effect. |
| READ_RESULT | After FLUSH and delivery of the requested result to the CPU. | Consumes the result slot and returns its bit. |
| END | After FLUSH and publication of closure. | Prevents further producer operations. Simulation continues until drain. |

FLUSH at the untouched empty origin closes it locally. Positive ADVANCE from
that origin or an already flushed group needs no group reply.

APPEND can retire before TCU admission. This allows several scalar instructions
to build one group. For example, at cursor 4, APPEND(A), APPEND(B), ADVANCE(3)
submits A and B for cycle 4 and moves the cursor to 7 after acknowledgment.

Only one sealed group may await acknowledgment. A blocked operation retains its
ID and operands across retries. The producer must neither submit it twice nor
allow a different operation to replace it.

An intrinsically oversized group raises a capacity fault. A full CPU result-slot
array also faults because a later READ_RESULT is needed to free it. Waiting is
allowed for a resource that earlier work can release independently, such as
fast-feedback delivery credits.

## Crossing and TCU edge order

For publication tick p, let r0 be the first receiver edge **strictly after p**.
With configured latency N receiver edges, N >= 1, the message becomes eligible at:

```text
eligible = r0 + (N - 1) * receiver_period
```

This rule applies to groups, replies and feedback. A mailbox retains its payload
until consumed; a notification is only a scheduling mechanism.

For example, with CPU period 5 ns, TCU period 20 ns and one-edge crossings:

| Tick | Earliest possible event |
| --- | --- |
| 20 ns | CPU publishes a sealed group. |
| 40 ns | TCU admits it and publishes the reply, if capacity and deadline permit. |
| 45 ns | CPU receives the admission reply. |

That group cannot be due at 40 ns: admission at its due tick is late.
Its firing may precede CPU receipt of the reply if the reply path is slower.

At each TCU rising edge:

1. Read existing queue state and eligible messages. If session reset applies,
   skip the ordinary transition.
2. Calculate the current logical cycle. If the existing head is due, match its
   complete manifest, evaluate conditions using existing history, and preflight
   the selected device actions.
3. Validate the candidate group's identity, mapping, deadline and capacity.
   Use queue occupancy from the **start of the edge**. A slot freed by firing
   on this edge becomes available on the next edge.
4. Validate incoming fast results without changing the history used in step 2.
   Any validation fault prevents this transition's firing and admission commits.
5. Remove the fired group, insert the admitted group, and emit their results.
   Commit incoming fast history for use on later edges.

At most one old point fires and one new group is admitted per edge. A newly
admitted group cannot fire on its admission edge. These steps run within one
TCU transition; they do not each consume a clock cycle.

## Start, deadlines and empty queues

With effective epoch start S and period P, logical cycle n is due at `S + n * P`.
Initially S is `profile.start`; session reset computes a new S as described below.
The start edge is cycle zero. Start is configured independently of CPU progress,
so a blocked producer cannot prevent the timer from starting.

Admission adds each timing interval to the last admitted due cycle, even if the
queue became empty between groups. For intervals 4 and 3, the due cycles are
4 and 7. The second point must be admitted before cycle 7; arrival does not
rebase it.

An empty timing queue does not stop the timer or cause an immediate fault.
The producer can still submit a future point before its deadline. A point due
at cycle zero must be admitted before start. Late arrival raises `LateAdmission`.

The timing point's manifest lists exact event IDs. A missing or extra member
faults the complete group. A port absent from the manifest remains idle.
An empty manifest is a valid wait-only point.

## Device batches and feedback

The device barrier waits for every CPU, memory and TCU transition due at tick t
to finish. It then processes all physical work assigned to t, including
zero-delay actions just launched by the TCU.

A launch batch belongs to one label firing. A physical boundary batch contains
all starts, ends, samples and ready results at one tick, potentially from several
launches. Output delays can spread one launch across several boundaries.

Before changing quantum state, the runtime validates the whole physical boundary batch.
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
The marker contains the last admitted label; zero denotes an empty stream.
Its mailbox envelope supplies epoch and visibility timing.

Success requires all of the following:

- The CPU has halted, the producer is closed, and the TCU has received closure.
- Timing and event queues, physical actions and scheduled readouts are empty.
- Memory transactions and communication mailboxes have drained.
- All CPU and enabled fast-feedback deliveries, including credit acknowledgments,
  have completed.

Unread Visible CPU slots may remain as final state. They do not count as pending
deliveries. Watchdog expiry reports a failed run rather than successful closure.

## Session reset

Reset takes precedence over ordinary work at its tick and starts a new epoch.
It clears CPU and producer state, mailboxes, TCU queues and history, device
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

[C++ interfaces](cpp-interfaces.md) define the actual records. `GroupReply`
acknowledges admission; `ProducerAccepted` and `GroupAdmitted` are trace events.
The trace schema is documented in [file formats](interfaces.md#jsonl-trace).

The [module pages](modules/README.md) identify source files and registered tests.
The [testing guide](engineering-and-testing.md) explains how to run them.
External CACTUS comparisons follow
[ADR 0002](decisions/0002-reference-comparison-scope.md); their tooling and reports
remain outside this repository.
