# Codeword decoding

Codeword decoding expands one port and codeword command into the device actions defined
by the profile. A command can select several actions on different physical ports,
such as acquisition and a separate discriminator arm.

## Connections

- **Input:** source port and codeword, profile, epoch, instruction ID, first event ID,
  and any measurement or condition token.
- **Output:** `OperationEvent` values for the timing control's pending events at the current time point.
- **Caller:** `TimingControl` during APPEND; timing point validation also runs when
  the timing control submits a timing point and when the TCU checks enqueue.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Pending events and codeword map"];
  owner [label="decode_codeword and validate_timing_events"];
  state [label="Profile::mappings\nOperationEvent\nTimingEvents"];
  output [label="OperationEvent values and validated TimingEvents"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records and configuration"];
}
```

## Resolving and validating actions

`decode_codeword()` looks up the mapping and creates one event per `EventSpec`.
It assigns consecutive event IDs while preserving the source instruction,
source port and codeword. Each event also names its physical output port,
resources, targets and timing parameters.

Acquisition and arm actions carry the measurement token already reserved by the
measurement result storage. Timing control collects the returned events at the current time point.
When the timing point is submitted, it assigns a label and builds a manifest containing
the exact event IDs.

`validate_timing_events()` checks those identities, the action mappings, acquisition and arm
pairing and per-port bounds. It validates a complete value before that value is
inserted into TCU queues. Lookup and validation add no separate simulated stage.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Profile::mappings` | read-only mappings | Maps a source port and codeword to one or more EventSpec values. |
| `OperationEvent` | output values | Resolved action plus epoch, event and instruction IDs and optional tokens. |
| `TimingEvents` | timing control-owned aggregate | Timing point, ordered manifest, events and profile fingerprint. |

[C++ API](../api.md#controlhpp).

## Reset and errors

Unknown mappings, inconsistent identities, invalid acquisition and arm pairs and
impossible timing point sizes raise typed faults. Timing control checks reject staging and
per-port capacity violations before accepting the APPEND.

Codeword decoding retains no mutable state. Reset discards the timing control's generated
events; the profile mappings remain unchanged. Multi-point microcode expansion
is not implemented.

## Implementation and tests

Source: [control.cpp](../../src/control.cpp) and [control.hpp](../../include/qsbit/control.hpp).

**CTest:** `control.mapping`, `protocol.capacity`.

The tests reject invalid mappings and manifests, and timing points that exceed
staging or result capacity.
