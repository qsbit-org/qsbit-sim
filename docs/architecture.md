# Controller architecture

Follow the command path from the CPU through the producer and TCU to the device
runtime. Measurement results return through two delivery paths: CPU result slots for QREAD
and fast-condition history for conditional TCU output. Delivery alone does not
imply that a program makes a conditional decision.

Click a box to open its module reference. Use the zoom controls to inspect the
connections, or open the [module list](modules/README.md) for text navigation.

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

## Read the diagram

Groups identify the main C++ owners and scheduling domains. Boxes identify
logical responsibilities; several boxes can belong to one owner. A state owner
is the object that changes that state, not an independent process.
Solid arrows carry values or calls. Dashed arrows show dependencies such as
scheduling, observation or stored state; each dependency is labeled. The same
convention applies to the individual module diagrams.

For example, a `Group` crosses from the producer to the TCU through a timed
mailbox. Resolving a port/codeword mapping is a direct C++ call inside command
preparation. Only the former adds the configured crossing delay.

## When each model runs

| Model | Trigger | Work |
| --- | --- | --- |
| CPU and producer | CPU rising edge | Receive eligible replies, advance the pipeline and execute producer operations. |
| Memory | CPU rising edge | Complete existing transactions and accept requests into previously idle slots. |
| TCU | TCU rising edge | Check the due group, admit a candidate using existing capacity, and commit new fast results. |
| Device runtime | Physical boundary, after coincident clocked work | Evolve state, end and start actions, sample measurements and publish ready results. |
| Reset and stop control | Edge callbacks, timed wakeup and barrier | Apply reset before ordinary work; stop after complete drain or a fault. |

`Simulator` connects these models to SystemC. It uses an explicit barrier for
device work and tick-stamped mailboxes for communication.
The [protocol reference](module-architecture.md#crossing-and-tcu-edge-order)
defines exact visibility and ordering.

To see these connections in use, [step through a feedback program](execution.md).
