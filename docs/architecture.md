# Single-controller architecture

The diagram shows how one controller schedules quantum operations and processes
measurement feedback.

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
:alt: Single-controller instruction scheduling, timing and event queues, control output, quantum device and measurement feedback.
```

[Distributed simulation](distributed-simulation.md) adds controller cores and
synchronization; [decoder feedback](decoding.md) connects classical decoding
through memory-mapped I/O.

In the simulator, `ControlElectronics` schedules physical events and checks
resources. `BackendExecution` batches operations for `IQuantumBackend`, which
supplies quantum-state evolution and measurement outcomes. See the
[implementation map](implementation.md#implementation-map) for these components,
or select a diagram component to inspect its source and tests.
