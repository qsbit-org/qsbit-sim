# Follow a program through the controller

Select a deterministic scripted run. **Next event** advances one recorded event;
**Next tick** skips to the first event at a later simulation tick. Playback speed
changes only the presentation. Several events can share one hardware timestamp.

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

## What the display means

Global tick is simulated time in nanoseconds. CPU cycle is the clock edge index;
between edges the display shows the last edge index. TCU logical cycle begins at
the configured start. Producer cursor is shown only when recorded by producer events,
as a **last observed value**. The player does not reconstruct private queue contents
or pipeline latches from retirement records.

The event list retains trace order within a tick. Independent owners may emit events
at the same tick; their list positions do not imply an extra cycle or a hardware
ordering dependency. Timeline marks share an x coordinate when their ticks match.
Click a timeline mark or choose a milestone to jump to its recorded event.

## Expected schedules

All examples use the scripted backend and the default profile. Outcomes are fixed;
these runs explain control timing, not numerical quantum-state evolution.

| Program | Physical operation starts (ns) | Result |
| --- | --- | --- |
| Bell | H: 1160; CX: 1200; both acquisitions: 1240 | Both scripted measurements return 1. |
| Feedback, outcome 1 | X: 1160; acquisition: 1240; feedback X: 3560 | Memory word 4096 contains 1. |
| Feedback, outcome 0 | X: 1160; acquisition: 1240; feedback Z: 3560 | Memory word 4096 contains 0. |

In each run, acquisition samples at 1280 ns, the discriminator result is ready at 1300 ns, CPU visibility begins at 1305 ns, and fast visibility begins at 1340 ns.

In Bell, both measurement APPENDs join one group. QREAD seals that group before
waiting for a CPU-visible result. In feedback, QREAD completes before the classical
branch chooses the final operation. Compare [producer behavior](modules/timeline-reservation-manager.md),
[TCU firing](modules/tcu-timer-and-label-broadcaster.md), and
[measurement visibility](modules/measurement-scoreboard-and-cpu-feedback.md).
