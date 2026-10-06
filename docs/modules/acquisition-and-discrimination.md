# Acquisition and discrimination

The controller models discriminator timing; the backend supplies measurement
bits and may model readout assignment internally. At acquisition end,
`ControlElectronics` requests a measurement through `BackendExecution`, which
executes pending backend work and returns the sampled bit. Results travel to
the CPU and, when enabled, the TCU after the discriminator delay.

## Connections

- **Input:** an acquisition event, its existing measurement reference and any
  separately mapped discriminator-arm event.
- **Output:** one tagged `Completion` to each enabled feedback path.
- **Owner:** `ControlElectronics::readouts_`, updated at acquisition, arm and result-ready
  physical boundaries.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Acquisition and discriminator triggers"];
  owner [label="ControlElectronics::readouts_"];
  state [label="Readout::reference\nReadout::{end, arm, ready}\nReadout::sample"];
  output [label="Completion to CPU and fast mailboxes"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## From acquisition to result

Timing control reserves the measurement reference and result-delivery
capacity before queue insertion. A discriminator arm enables discrimination
for that acquisition. With `separate_arm: true`, the mapping must include one
`ArmSpec` event with the same measurement reference and target. Otherwise
arming occurs at acquisition start.

For acquisition end E, arm start A and discriminator delay L, sampling
occurs at E and the result becomes ready at `max(E, A) + L`.

An acquisition ending at 480 ns with an earlier arm and a 20 ns delay
therefore produces `ResultReady` at 500 ns. With the default communication
latencies, the CPU receives it at 505 ns and the TCU commits it at 540 ns.

The model retains the sampled bit until readiness, publishes it to each
enabled result path and removes the readout record. Even when L is zero,
receivers consume the result only on a later clock edge.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Readout::reference` | `MeasurementReference` | Measurement identity reserved by the timing control and measurement result storage. |
| `Readout::{end, arm, ready}` | global ticks | Acquisition end, arm start and result-ready ticks. |
| `Readout::sample` | `optional<bool>` | Empty until the backend samples the measurement at acquisition end. |
| `ControlLinks::cpu_results and fast_results` | independent mailboxes | One completion published to each enabled delivery path. |

[C++ API](../api.md#devicehpp).

## Reset and errors

Missing or duplicate arms, mismatched targets or measurement references, and overflowing
readiness times fail validation.

Session reset removes pending readouts and physical callbacks. Receivers reject
or discard stale completions according to their epoch checks.

## Implementation and tests

Source: [device.cpp](../../src/device.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `protocol.readout`.

The readout tests check delayed arms, exact sampling and readiness ticks, measurement reference
matching, and delivery through the separate CPU and fast-feedback paths.
