# Port and codeword mapping

The port and codeword mapping defines what a control instruction means for the
configured device. For example, the default map uses codeword 1 on port 0 for an
X gate on qubit 0, and codeword 4 for acquisition on that qubit.

The mapping belongs to the simulation profile. Programs select entries by source port
and codeword; the selected events can address different core-local output ports.

## Connections

- **Input:** source port and codeword from a `cw` operation.
- **Output:** a `Mapping` containing one or more `EventSpec` values for codeword decoding.
- **Owner:** the immutable `Profile`; lookup has no runtime state.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Port and codeword"];
  owner [label="Profile::mapping"];
  state [label="Mapping::port and codeword\nMapping::actions\nEventSpec"];
  output [label="Mapping with EventSpec values"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records and configuration"];
}
```

## Event specifications

Each `EventSpec` defines an ideal gate, pulse drive, acquisition,
discriminator arm or one output of a paired gate. It specifies the output port,
delay and duration. Pulses also have an axis and amplitude; acquisitions
have discriminator timing. `execution_flag` selects the target qubit's flag
for single-qubit gates and pulses.

A paired output names a `TwoQubitGate` in `Profile::two_qubit_gates`. That
definition supplies the backend operation, two targets, resources, duration and
two required input endpoints. The default CX uses port 0 with codeword 6 and
port 1 with codeword 10. See [two-port gate outputs](../interfaces.md#two-port-gate-outputs).

The default map assigns codewords 7, 8 and 9 to X gates controlled by
`last_one`, `last_zero` and `equal`, respectively, on each qubit's port.

The event starts at `fire_tick + delay` and occupies its port and
resources over `[start, end)`. Duration must be positive. An ideal gate
changes quantum state at the start of that interval.

Backend support is checked when a program requests the mapping. A profile
can therefore contain unused pulse entries when running the Aer backend.
Waveforms belong to backend configuration. A pulse mapping selects a waveform
by `operation`; QuTiP supports `square`, `gaussian`, `drag` and `samples` shapes.
See [waveform configuration](../qutip.md#waveforms).

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Mapping::port and codeword` | lookup key | Source control port and digital codeword. |
| `Mapping::actions` | `vector<EventSpec>` | IdealGate, Pulse, Acquire, DiscriminatorArm or GateOutput descriptors. |
| `EventSpec` | port and timing with a typed payload | Holds one of `GateSpec`, `PulseSpec`, `AcquireSpec`, `ArmSpec` or `GateOutputSpec`. |

[C++ API](../api.md#controlhpp).

## Reset and errors

Profile validation rejects malformed or duplicate mappings and invalid timing
parameters. Lookup rejects unknown ports or codewords. An unsupported requested
event fails before timing control acceptance.

Session reset preserves the map. Select a new profile before constructing a new
simulator to change these definitions.

## Implementation and tests

Source: [control.cpp](../../src/control.cpp) and [control.hpp](../../include/qsbit/control.hpp).

**CTest:** `config.profile`, `control.mapping`, `protocol.readout`.

The tests reject unknown port and codeword pairs, duplicate mappings and
incompatible acquisition and arm targets. They also check that changing a mapping
changes the profile fingerprint. `config.profile` checks JSON round trips and
rejects fields belonging to another event kind.
