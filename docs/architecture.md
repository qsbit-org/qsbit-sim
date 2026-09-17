# Architecture explorer

Click a box to open its behavior contract. The groups name the actual C++ owners;
the boxes name logical responsibilities. `Simulator` is the SystemC module that
schedules these owners. Pure helpers and queue substates add no clock stage.

Solid arrows carry requests, results or direct calls. Dashed arrows denote activation
or observation. The labels identify the values transferred; mailboxes enforce the
[strict receiver-edge rule](module-architecture.md#42-crossing-and-tcu-edge-order).

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
:alt: Clickable qsbit-sim architecture, grouped into setup, CPU, TCU, device and lifecycle owners.
```

For a keyboard-accessible list of every box, use the [module index](modules/README.md).
For a concrete execution, open the [trace player](execution.md).

## Scheduling and ownership

| Owner | Activation | Work committed |
| --- | --- | --- |
| CPU and producer | `Simulator::cpu_edge`, CPU rising edge | Receive eligible replies; advance the pipeline; execute producer operations. |
| Memory | `Simulator::memory_edge`, CPU rising edge | Complete old fetch/data transactions and accept eligible requests. |
| TCU | `Simulator::tcu_edge`, TCU rising edge | Check old queues/history; preflight firing; admit using old capacity; publish launch and replies. |
| DeviceRuntime | `Simulator::barrier`, when a physical boundary is due | Process that tick after coincident CPU, memory and TCU transitions have completed. |
| Lifecycle | Reset wakeup, edge callbacks and barrier | Reset takes precedence; successful stop waits for complete drain. |

A diagram arrow is not an additional cycle. `Group` crosses to the TCU through a
mailbox; resolving a codeword inside the producer is a direct C++ call. The TCU's
logical cycle is calculated from `(tick - start) / period` after start. The producer
cursor is separately stored CPU-side state describing the point being prepared.
