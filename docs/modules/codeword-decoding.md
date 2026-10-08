# Codeword decoding

Codeword decoding expands one port and codeword command into the device events defined
by the profile. A command can select several events on different core-local output ports,
such as acquisition and a separate discriminator arm.

## Connections

- **Input:** source port and codeword, profile, epoch, instruction ID, first event ID,
  and an optional measurement reference.
- **Output:** `OperationEvent` values for the timing control's pending events at the current time point.
- **Caller:** `TimingControl` while executing `cw`; time point validation also runs when
  the timing control submits a time point and when the TCU checks enqueue.

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

## Resolving and validating events

`decode_codeword()` creates one `OperationEvent` for each `EventSpec`
in the selected mapping. It assigns consecutive event IDs and preserves
the instruction ID, source port and codeword.

A `GateOutput` references a shared two-qubit gate definition. Decoding retains
the output port and codeword; the device checks the required pair at their
physical start tick before applying the gate.

Acquisition and discriminator-arm events carry the measurement reference
reserved by `MeasurementRegisters`. Timing control collects the events
for the current time point. On submission, it assigns their timing label
and records their IDs in `TimingPoint::manifest`.

`validate_timing_events()` checks IDs, mappings and per-port limits before
queue insertion. Device validation checks acquisition and arm pairing.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Profile::mappings` | read-only mappings | Maps a source port and codeword to one or more EventSpec values. |
| `OperationEvent` | output values | Resolved event plus epoch, event and instruction IDs and optional measurement references. |
| `TimingEvents` | timing control-owned aggregate | Time point, ordered manifest, events and profile fingerprint. |

[C++ API](../api.md#control_protocolhpp).

## Reset and errors

Unknown mappings, inconsistent identities, invalid acquisition and arm pairs and
event counts above the configured limits raise faults. Timing control checks
staging and per-port limits before accepting `cw`.

Reset discards pending events while preserving the profile mappings.
One codeword expands into events at one time point.

## Implementation and tests

Source: [control_protocol.cpp](../../src/control_protocol.cpp) and [control_protocol.hpp](../../include/qsbit/control_protocol.hpp).

**CTest:** `control.mapping`, `protocol.capacity`.

The tests reject invalid mappings and manifests, and time points that exceed
staging or result capacity.
