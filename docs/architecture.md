# Single-controller architecture

The processor prepares port and codeword events during the reserve phase.
The TCU stores time points and per-port events, then triggers control output
at the requested times. Measurement results feed classical control and fast
conditional execution.

The diagram shows instruction scheduling and measurement feedback within one
controller. [Distributed simulation](distributed-simulation.md) adds multiple
cores and inter-controller synchronization; [decoder feedback](decoding.md)
connects classical decoding through memory-mapped I/O.

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

The timing controller uses timing labels to identify the events belonging to
each time point, as in [QuMA, Section 5.2](https://arxiv.org/abs/1708.07677).
The [overview](high-level-design.md) explains the instruction and feedback scope.
Click a component to inspect its implementation and tests.

In the simulator, `ControlElectronics` schedules physical events and checks
resources. `BackendExecution` batches the resulting operations for
`IQuantumBackend`, which supplies quantum-state evolution and measurement
outcomes. See the [implementation map](implementation.md#implementation-map).

See [simulation timing](module-architecture.md) for edge order and communication
delays, or [recorded executions](execution.md) for example traces.
