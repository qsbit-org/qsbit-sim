# Follow a program through the controller

Select **feedback-one** to follow a measurement through the controller and
see how the CPU selects the next gate. [Quickstart](quickstart.md) runs the
same program locally.

Use **Next event** to advance one trace record or **Next tick** to move to
the next timestamp. Speed changes playback, not simulation timing.

To replay a local trace, run
`python3 tools/replay_trace.py PATH/TO/TRACE.jsonl` from the repository root.
The player uses the matching `.json` summary when available to derive clock
values. Press Ctrl+C to stop the server.

```{raw} html
<div id="trace-player" class="trace-player" aria-label="Simulation trace player">
  <p id="trace-status" role="status">Loading recorded executions…</p>
  <div class="player-controls">
    <label>Program <select id="trace-example" aria-label="Program"></select></label>
    <button id="trace-prev" type="button">Previous</button>
    <button id="trace-play" type="button">Play</button>
    <button id="trace-next" type="button">Next event</button>
    <button id="trace-tick" type="button">Next tick</button>
    <label>Speed <select id="trace-speed"><option value="600">1×</option><option value="200">3×</option><option value="60">10×</option></select></label>
  </div>
  <label class="scrubber">Recorded event <input id="trace-position" type="range" min="0" max="0" value="0"></label>
  <div id="trace-clocks" class="clock-grid"></div>
  <div id="trace-owners" class="owner-grid"></div>
  <div class="timeline-scroll"><svg id="trace-timeline" role="img" aria-label="Events by owner and simulation tick"></svg></div>
  <div class="player-columns">
    <section><h2>Recorded event</h2><select id="trace-jump" aria-label="Jump to milestone"></select><pre id="trace-event"></pre></section>
    <section><h2>Last observed state</h2><div id="trace-observed"></div></section>
  </div>
  <details><summary>Input assembly</summary><pre id="trace-program"></pre></details>
  <details><summary>Run configuration</summary><pre id="trace-config"></pre></details>
</div>
<noscript>Enable JavaScript to use the execution player.</noscript>
```

## Follow the feedback path

1. Select **feedback-one** and open **Input assembly**. The program prepares
   qubit 0, measures it and branches on the result.
2. Jump to `ProducerAccepted`. Its `cycle` is the current time point: the TCU
   cycle being prepared. Acceptance means the event is staged at the CPU.
3. Find `GroupAdmitted` and then `LabelFired`. The first records queue insertion;
   the second records the planned TCU triggering edge.
4. Follow `MeasurementSampled`, `ResultReady` and `CpuResultVisible`. These show
   sampling, discriminator delay and delivery to the CPU.
5. Continue to the last `OperationStart`. With outcome 1, the program selects X
   on qubit 1. Select **feedback-zero** to see the same branch select Z.

Click the component cards to read the relevant module behavior.

## Read the displayed values

Global tick is simulation time in nanoseconds. CPU edge index and TCU logical
cycle are derived from the tick and profile. Between edges, the display
retains the last index. These examples have no reset, so TCU cycle zero
occurs at `profile.start`.

**Last observed state** shows recorded values with their observation ticks.
Queue occupancy, for example, comes from the last enqueue record. The player
does not update it between observations.

Timeline lanes collect related records. **Result delivery** includes both
CPU delivery and TCU result commits. Records with the same tick share a
horizontal position.

## Example schedules

The examples use mock measurement bits and set TCU start to 200 ns.
Other timing settings use their defaults. The first operation at cycle 8 starts
at 360 ns, after the CPU has had time to prepare the queued time points.

| Program | Physical starts (ns) | Outcome |
| --- | --- | --- |
| Bell | H: 360; CX: 400; both acquisitions: 440 | Both mock bits are 1. |
| Feedback, outcome 1 | X: 360; acquisition: 440; branch-selected X: 720 | Memory word 4096 is 1. |
| Feedback, outcome 0 | X: 360; acquisition: 440; branch-selected Z: 720 | Memory word 4096 is 0. |

In each run, acquisition samples at 480 ns. The result is ready at 500 ns,
CPU-visible at 505 ns and committed to conditional results at 540 ns. With the 20 ns
TCU period, conditions can first use that history at 560 ns. `FastResultVisible`
marks the commit, not use by the condition check already performed at 540 ns.
A later condition can use that result only while it remains stored.

In Bell, two measurement QAPPEND instructions join one time point before QREAD submits it.
In feedback, QREAD completes before the classical branch chooses the final
operation. Both branches reach enqueue at 680 ns and schedule the selected
gate for cycle 26 (720 ns), leaving two TCU cycles before output. Neither
waiting for a result nor an empty queue pauses the TCU timer.

The mock backend demonstrates control timing. Use the
[Aer example](backends.md#install-an-optional-backend) to obtain bits from
quantum-state evolution.
