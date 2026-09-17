# Synchronization Extension Boundary

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Supported behavior

Distributed synchronization is unsupported. The decoder recognizes the reserved QSYNC
encoding; the producer raises `UnsupportedSynchronization` when that instruction executes.
There is no synchronization process, peer-link state, or TCU pause/resume path.

| Direction | Contract |
| --- | --- |
| Upstream | CPU executes a legally encoded QSYNC instruction. |
| Downstream | Typed fatal fault, recorded by Simulator; no successful instruction completion. |
| State | No synchronization-specific retained state. |
| Activation | Producer call from the CPU edge transition. |
| Time | Rejection occurs on execution; it does not book a timing point or pause the TCU. |
| Reset | Normal session reset applies; there are no peer messages to cancel. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="QSYNC"];
  owner [label="TimelineProducer rejection"];
  state [label="No retained synchronization state"];
  output [label="UnsupportedSynchronization"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Objects and state transition

QSYNC decodes to ProducerKind::Synchronize and execute raises UnsupportedSynchronization. There is no synchronization-specific object, peer-link queue or TCU pause state.

[Current C++ declarations](../api.md#producerhpp).

## Implementation and verification

- Implementation: [producer.cpp](../../src/producer.cpp), [producer.hpp](../../include/qsbit/producer.hpp).
- Encoding: [quantum instruction interface](../interfaces.md#quantum-instruction-encoding).


**CTest:** `systemc.use_cases`.

The `unsupported` program executes QSYNC and requires `UnsupportedSynchronization`.

## Future extension

A distributed profile would need peer-message timing, synchronization booking, TCU
pause/resume rules and reset handling. While a local TCU is paused, global simulation
time and device evolution must continue. Those behaviors have no implementation or
passing synchronization tests in the current baseline.
