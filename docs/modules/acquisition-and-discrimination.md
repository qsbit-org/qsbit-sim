# Acquisition and Discrimination

Readout separates measurement sampling from result availability.
`DeviceRuntime` samples the backend at acquisition end, then waits for the
discriminator timing before publishing the bit to the CPU and optional fast path.

## Connections

- **Input:** an acquisition action, its existing measurement token and any
  separately mapped discriminator-arm action.
- **Output:** one tagged `Completion` to each enabled feedback path.
- **Owner:** `DeviceRuntime::readouts_`, updated at acquisition, arm and result-ready
  physical boundaries.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Acquisition and discriminator triggers"];
  owner [label="DeviceRuntime::readouts_"];
  state [label="Readout::token\nReadout::end / arm / ready\nReadout::sample"];
  output [label="Completion to CPU and fast mailboxes"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## From acquisition to result

The producer has already reserved the measurement token and delivery capacity.
Readout does not allocate another token. If acquisition requires a separate arm,
group validation requires one matching arm with the same token and target.
Otherwise the discriminator is implicitly armed at acquisition start.

Let E be acquisition end, A the arm start and L the discriminator delay.
The backend samples and collapses state at E; the bit becomes ready at
`max(E, A) + L`.

For example, an acquisition ending at 480 ns with an earlier arm and a 20 ns
delay produces `ResultReady` at 500 ns. With the default crossings, the CPU
receives the bit at 505 ns and the TCU commits it at 540 ns.

The runtime stores the sample until readiness, publishes the same token and bit
to both enabled paths, then removes the readout record. A zero discriminator
delay can make sampling and readiness share a tick. It still cannot make a
clocked receiver consume the result on that tick.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Readout::token` | `Token` | Measurement identity reserved by the producer/scoreboard. |
| `Readout::end / arm / ready` | global ticks | Acquisition end, arm start and result-ready ticks. |
| `Readout::sample` | `optional<bool>` | Empty until the backend samples the measurement at acquisition end. |
| `ControlLinks::cpu_results / fast_results` | independent mailboxes | One completion published to each enabled delivery path. |

[C++ API](../api.md#devicehpp).

## Reset and errors

Missing or duplicate arms, mismatched targets or tokens, and overflowing
readiness times fail validation. The backend supplies measurement bits; raw
waveform discrimination is not implemented.

Session reset removes pending readouts and physical callbacks. Receivers reject
or discard stale completions according to their epoch checks.

## Implementation and tests

Source: [device.cpp](../../src/device.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `protocol.readout`.

The readout tests check delayed arms, exact sampling and readiness ticks, token
matching, and delivery through the separate CPU and fast-feedback paths.
