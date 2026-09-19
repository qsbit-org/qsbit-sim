# Trace Recorder and Stop Controller

`Trace` records what the simulator has done. `Simulator` decides when a run
has completed or failed. Together they provide the event history and final
status used to inspect a run.

## Connections

- **Input:** committed model events, producer closure, drain state and faults.
- **Output:** JSONL trace records, success or failure status, and a stop tick.
- **Scheduling:** models emit observations during transitions; the device barrier
  checks for successful completion after work at the current tick.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Committed domain events"];
  owner [label="Trace / Simulator"];
  state [label="Trace::events_\nSimulator::stopped_ / success_ / fault_\nOwner drain predicates"];
  output [label="JSONL / success or typed fault"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Recording and finishing a run

Trace events distinguish staging, admission, label firing, physical operation
starts and ends, sampling, readiness and feedback visibility. Each event has
a timestamp and kind-specific identities. Recording an event consumes no
simulated time.

A `Completion` record carries a measurement bit; it is separate from whole-run
completion. QEND stops CPU production after the producer has flushed and received any
required admission reply. Simulation continues until the TCU has seen closure,
queues and physical actions have drained, memory is idle, and every enabled
feedback message and credit acknowledgment has been delivered.

A Visible CPU result slot may remain unread at successful stop. It is final
state, not an outstanding delivery. The barrier emits `SimulationCompleted`
only when all completion conditions hold.

Trace ticks are nondecreasing. Records at the same tick do not represent extra
hardware cycles; use their event meanings and the protocol to interpret order.
The [trace reference](../interfaces.md#jsonl-trace) defines fields and ID namespaces.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Trace::events_` | `vector<TraceEvent>` | Append-only observation records, ordered by nondecreasing tick. |
| `Simulator::stopped_ / success_ / fault_` | terminal state | Distinguishes full drain from fatal failure. |
| Owner drain predicates | read-only checks | CPU, producer, TCU, device, scoreboard, links and memory completion. |

[C++ API](../api.md#tracehpp).

## Reset and errors

A fatal fault records its type and explanation, marks the run unsuccessful
and stops SystemC. The simulation watchdog is a deadline in global simulation ticks, not a host-time
timeout. Its expiry is a failure to drain. The application writes
the partial trace and failed summary when the output paths remain usable.

Reset starts a new epoch in the same trace. Aborted actions and observed stale
completion discards remain available for diagnosis.

## Implementation and tests

Source: [simulator.cpp](../../src/simulator.cpp) and [trace.hpp](../../include/qsbit/trace.hpp).

**CTest:** `systemc.use_cases`.

The tests compare complete traces after reversing process registration. They
also check that END waits for slow fast-feedback delivery, that reset changes
the epoch, and that watchdog and model faults terminate with the expected type.
