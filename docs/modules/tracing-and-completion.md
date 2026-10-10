# Tracing and completion

`Trace` records what the simulator has done. `Simulator` decides when a run
has completed or failed. Together they provide the event history and final
status used to inspect a run.

## Connections

- **Input:** committed model events, timing control closure, drain state and faults.
- **Output:** JSONL trace records, success or failure status, and a stop tick.
- **Scheduling:** models emit observations during transitions; the device barrier
  checks for successful completion after work at the current tick.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="TraceEvent records"];
  owner [label="Trace and Simulator"];
  state [label="Trace::events_\nSimulator::stopped_ and success_ and fault_\nCore::drained, ControlElectronics::drained\nSyncNetwork::empty, DecoderSystem::idle"];
  output [label="JSONL and success or typed fault"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Recording and finishing a run

Trace records distinguish event preparation, queue insertion, triggering,
output starts and ends, measurement sampling, result readiness and delivery.
Each has a tick and event-specific IDs. CPU retirement, CPU pipeline snapshots and
decoder events use distinct C++ payload types. The serializer writes their JSONL
fields, and the recorder rejects a kind that does not match its payload. See the
[trace reference](../interfaces.md#jsonl-trace).

The exit ECALL preserves `a0` as the program exit status and halts the CPU after its pending events have been enqueued and
acknowledged. The simulation continues until the TCU receives closure,
all queued and device work finishes, memory is idle, and all enabled
result deliveries and acknowledgments complete. Multicore runs require every
core and all synchronization connections to drain before stopping the shared device.
Configured decoders must also finish all requests and complete their input windows.

When these conditions hold, the device evolves the backend state to the current
tick before the simulator emits `SimulationCompleted`. Trace ticks are
nondecreasing; several records can share one tick.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Trace::events_` | `vector<TraceEvent>` | Append-only observation records, ordered by nondecreasing tick. |
| `Simulator::stopped_ and success_ and fault_` | terminal state | Distinguishes full drain from fatal failure. |
| Component completion checks | read-only checks | Every core's CPU, timing control, TCU, measurement result storage, links and memory; shared `ControlElectronics`, `SyncNetwork` and `DecoderSystem`. |

[C++ API](../api.md#tracehpp).

## Reset and errors

A fatal fault records its type and explanation, marks the run unsuccessful
and stops SystemC. The watchdog checks a global simulation deadline.
If the run has not drained, it fails with `Watchdog`. The application writes
the partial trace and failed summary when the output paths are usable.

Reset starts a new epoch in the same trace. Aborted events and observed stale
completion discards remain available for diagnosis.

## Implementation and tests

Source: [simulator.cpp](../../src/simulator.cpp) and [trace.hpp](../../include/qsbit/trace.hpp).

**CTest:** `cpu.trace`, `decoder.transport`, `systemc.use_cases`.

CPU and decoder tests check typed records, payload validation and serialization.
The integration tests compare complete traces after reversing process registration. They
also check that simulation completion waits for slow fast-feedback delivery, that reset changes
the epoch, and that watchdog and model faults terminate with the expected type.
