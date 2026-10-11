---
html_theme.sidebar_secondary.remove: true
---

# Follow a program through the controller

Watch instructions prepare time points, queues feed control outputs, and
measurement results return to the CPU. Select **feedback-one** to follow a
measurement-dependent branch. [Quickstart](quickstart.md) runs the same program locally.

```{raw} html
<div id="trace-player" class="trace-player" aria-label="Simulation trace player"></div>
<noscript>Enable JavaScript to use the execution player.</noscript>
```

## Explore an execution

Use **Play** to follow execution and **Next time** to apply all records at the
next timestamp. Select an output interval to inspect its start. **Next record**,
under **Trace records**, advances one record, including within the same timestamp.

**Machine state** shows the controller and the selected module's details.
**Instructions** lists retired instructions; **Measurements** shows sampling,
readiness and delivery. In multicore traces, **Core** selects the controller;
the output timeline includes all cores.

Highlighted modules changed at the selected time. CPU slots show end-of-edge
occupancy recorded by `CpuPipelineUpdated`; the register display keeps nonzero
values and destinations written to zero. Queue entries follow enqueue and
trigger records; output durations come from the configured mappings.

The CPU edge index is derived from its configured clock. The TCU cycle is the
latest recorded cycle at or before the selected timestamp. Playback speed does
not change simulation time.

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
