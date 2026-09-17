# Follow a program through the controller

This page follows real simulator traces for the Bell and measurement-feedback
examples. Start with **feedback-one** to see a measurement reach the CPU and
select the next quantum operation. [Quickstart](quickstart.md) shows how to
run that program locally.

Use **Next event** to advance one record or **Next tick** to move to the next
simulation timestamp. The speed control changes playback only.

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
<noscript>The interactive player requires JavaScript. The timing table and module contracts below remain available.</noscript>
```

## Follow the feedback path

1. Select **feedback-one** and open **Input assembly**. The program prepares
   qubit 0, measures it and branches on the result.
2. Jump to `ProducerAccepted`. Its `cycle` is the producer cursor: the TCU
   cycle being prepared. Acceptance means the action is staged at the CPU.
3. Find `GroupAdmitted` and then `LabelFired`. The first records queue insertion;
   the second records the planned TCU firing edge.
4. Follow `MeasurementSampled`, `ResultReady` and `CpuResultVisible`. These show
   sampling, discriminator delay and delivery to the CPU.
5. Continue to the last `OperationStart`. With outcome 1, the program selects X
   on qubit 1. Select **feedback-zero** to see the same branch select Z.

Click the component cards to read the relevant module behavior.

## Read the displayed values

Global tick is simulation time in nanoseconds. The displayed CPU edge index and
TCU logical cycle are derived from that tick and the profile. Between CPU edges, the
display retains the last edge index. TCU logical cycles begin at the configured
start.

**Last observed state** shows only values recorded by earlier events, together
with their observation tick. For example, occupancy is the value recorded at
the last admission; it is not a live view of the queue. Retirement records do
not reveal current pipeline latches.

Several events can share a timestamp. They appear at the same horizontal
position on the timeline. Moving to the next record at that tick does not
advance a hardware cycle.

## Example schedules

The examples use the default profile and scripted measurement bits.
The website build runs their ELF programs and checks the schedules before
publishing the playback data.

| Program | Physical starts (ns) | Outcome |
| --- | --- | --- |
| Bell | H: 1160; CX: 1200; both acquisitions: 1240 | Both scripted bits are 1. |
| Feedback, outcome 1 | X: 1160; acquisition: 1240; branch-selected X: 1520 | Memory word 4096 is 1. |
| Feedback, outcome 0 | X: 1160; acquisition: 1240; branch-selected Z: 1520 | Memory word 4096 is 0. |

In each run, acquisition samples at 1280 ns. The result is ready at 1300 ns,
CPU-visible at 1305 ns and committed to fast history at 1340 ns. TCU conditions
can use that history on later edges.

In Bell, two measurement APPENDs join one group before QREAD seals it.
In feedback, QREAD completes before the classical branch chooses the final
operation. Both branches reach admission at 1480 ns and schedule the selected
gate for cycle 26 (1520 ns), leaving two TCU cycles before output. Neither
waiting for a result nor an empty queue pauses the TCU timer.

The scripted backend demonstrates control timing. Use the
[Aer example](backends.md#install-an-optional-backend) to obtain bits from
quantum-state evolution.
