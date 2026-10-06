# Follow a program through the controller

Watch instructions prepare time points, queues feed control outputs, and
measurement results return to the CPU. Select **feedback-one** to follow a
measurement-dependent branch. [Quickstart](quickstart.md) runs the same program locally.

```{raw} html
<div id="trace-player" class="trace-player" aria-label="Simulation trace player"></div>
<noscript>Enable JavaScript to use the execution player.</noscript>
```

## Explore an execution

**Play** advances through state changes, skipping repeated stall records. **Next time** applies all records
at the next timestamp. Drag the slider or select a milestone to seek; select an
output interval to inspect its start. **Expand view** opens the full-width diagram.

Highlighted modules changed at the selected time. The CPU shows the outstanding
fetch request, fetch buffer, decode slot and execute slot. Each occupied slot
shows its instruction ID and PC. Changed slots are highlighted; blocked execute
instructions and discarded fetches are marked. Retirement appears beneath the
slots with its timestamp.

The register display shows nonzero registers; the destination remains visible when
written to zero. Timing Queue entries follow enqueue and trigger records.
Port queues show events resolved from their configured codeword mappings. Control outputs show
active operations and their configured durations.

The CPU edge index is derived from its configured clock. The displayed TCU cycle
is the latest cycle recorded at or before the selected timestamp.
CPU slots show end-of-edge occupancy from `CpuPipelineUpdated`.
In multicore traces,
**Core** selects the controller; control outputs include all cores.

Open **Trace records and configuration** to step through individual records,
including records at the same timestamp. Playback speed does not change simulation time.

## Follow measurement feedback

Select **feedback-one**, then **Measure**. Qubit 0 is sampled at 480 ns.
The result becomes ready at 500 ns, reaches the CPU at 505 ns, and updates
execution flags at 540 ns. These stages appear separately in **Measurement delivery**.

Continue to 720 ns: the CPU-selected X operation starts on qubit 1.
Select **feedback-zero** to see the same branch choose Z. In **bell**, two
port outputs at 400 ns produce one joint CX gate.

These recordings use mock measurement outcomes. Use the
[Aer backend](backends.md#install-an-optional-backend) for quantum-state evolution.

## Replay a local trace

From the repository root:

```sh
python3 tools/replay_trace.py PATH/TO/TRACE.jsonl
```

For `TRACE.jsonl`, the player first looks for `TRACE.summary.json`, then
`TRACE.json` in the same directory. It loads clock settings and core configurations
from that file. Use `--summary PATH/TO/SUMMARY.json` for a different
filename. Without a summary, recorded events remain available; derived clock
values are unavailable. Press Ctrl+C to stop the server.
