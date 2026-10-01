# Controller architecture

The processor prepares port and codeword events during the reserve phase.
The TCU stores time points and per-port events, then triggers control output
at the requested times. Measurement results feed classical control and fast
conditional execution.

```{raw} html
<div class="diagram-controls" aria-label="Architecture diagram controls">
  <button id="diagram-out" type="button" aria-label="Zoom out">−</button>
  <button id="diagram-in" type="button" aria-label="Zoom in">+</button>
  <button id="diagram-fit" type="button">Fit width</button>
  <button id="diagram-actual" type="button">Readable size</button>
  <a id="diagram-open" target="_blank" rel="noopener">Open full diagram</a>
</div>
```

```{graphviz} architecture.dot
:alt: Reserve phase, timing and per-port event queues, trigger phase, control output, quantum device and measurement feedback.
```

The timing controller uses timing labels to identify the events belonging to
each time point, as in [QuMA, Section 5.2](https://arxiv.org/abs/1708.07677).
The [overview](high-level-design.md) explains the instruction and feedback scope.
Click a component to inspect its implementation and tests.

See [simulation timing](module-architecture.md) for edge order and communication
delays, or [recorded executions](execution.md) for example traces.
