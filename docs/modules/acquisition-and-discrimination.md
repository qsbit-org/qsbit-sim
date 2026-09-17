# Acquisition and Discrimination

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

DeviceRuntime-owned readout protocol substate and scheduled result-ready events.

| Direction | Contract |
| --- | --- |
| Upstream inputs | Readout acquisition action, optional discriminator arm carrying the same measurement token, and backend sample. |
| Downstream outputs | Tagged completion to CPU-feedback crossing and independent fast-condition path. |
| State owner and retained state | Positive acquisition interval, arm tick, sample and collapse tick, result-ready tick, token status and reserved path credits. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Acquisition and discriminator triggers"];
  owner [label="DeviceRuntime::readouts_"];
  state [label="Readout::token; Readout::end / arm / ready; Readout::sample"];
  output [label="Completion to CPU and fast mailboxes"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Acquisition start and end are physical boundary events; result readiness is a timed callback.

**Transition:** Allocate no new token. For a separate acquisition and discriminator arm, group validation requires exactly one of each with the same token. Baseline sampling and collapse occur at acquisition end. With arm tick a and processing delay L, ready tick is max(acquisition_end,a)+L. Fan out the same completion to each reserved delivery path.

**Time and visibility:** A backend call returning early on the host does not publish the result. Even L=0 still obeys the later receiver-edge crossing rule.

**Reset and errors:** Missing or duplicate arm, unknown token, and unsupported raw-waveform discrimination are faults. Reset clears old scheduled physical work; mailbox receivers discard stale completions.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `Readout::token` | `Token` | Measurement identity reserved by the producer/scoreboard. |
| `Readout::end / arm / ready` | `global ticks` | Acquisition end, discriminator arm and max(end,arm)+delay. |
| `Readout::sample` | `optional<bool>` | Unset before acquisition end; populated by backend.measure. |
| `ControlLinks::cpu_results / fast_results` | `independent mailboxes` | One completion published to each enabled delivery path. |

accept validates acquisition/arm pairing and records the existing token. At acquisition end process calls the backend and stores the outcome. At ready tick it publishes Completion and erases readout state. Even zero discriminator delay uses strict receiver-edge visibility. Reset discards scheduled readouts; no old physical boundary survives.

[Current C++ declarations](../api.md#devicehpp).

## Implementation and verification

DeviceRuntime holds readout token, sample, arm and readiness state. Readiness is max(acquisition end, arm start) plus discriminator delay.

- Implementation: [device.cpp](../../src/device.cpp) and [device.hpp](../../include/qsbit/device.hpp).

**CTest:** `protocol.readout`.

Checks delayed arm, sample/readiness ticks, token matching and separate CPU/fast visibility.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
