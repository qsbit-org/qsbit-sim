# Port-Codeword Action Map

The port/codeword map defines what a control instruction means for the
configured device. For example, the default map uses codeword 1 on port 0 for an
X gate on qubit 0, and codeword 4 for acquisition on that qubit.

The mapping belongs to the simulation profile. Programs select entries by source port
and codeword; the selected actions can address different physical output ports.

## Connections

- **Input:** source port and codeword from an APPEND operation.
- **Output:** a `Mapping` containing one or more `ActionSpec` values for the lowerer.
- **Owner:** the immutable `Profile`; lookup has no runtime state.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Port and codeword"];
  owner [label="Profile::mapping"];
  state [label="Mapping::port / codeword\nMapping::actions\nActionSpec"];
  output [label="Mapping with ActionSpec values"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records / configuration"];
}
```

## Action descriptors

An action specifies its kind, physical port, targets, resources, output delay
and duration. The supported kinds are ideal gate, constant pulse, acquisition
and discriminator arm. Pulse actions include an axis and amplitude; readout
actions include discriminator timing.

An action begins at its group's label-fire tick plus its configured delay.
A zero delay allows firing and physical start at the same tick. Every action
still has a positive duration, which reserves its port and resources over
`[start, end)`. An ideal gate changes quantum state at the start of this interval.

The producer checks backend support when a mapping is requested. A shared
profile may therefore include unused pulse entries even when the selected
backend supports only gates. Arbitrary sampled waveforms and oscillator-register
operations are not implemented.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Mapping::port / codeword` | lookup key | Source control port and digital codeword. |
| `Mapping::actions` | `vector<ActionSpec>` | IdealGate, Pulse, Acquire or DiscriminatorArm descriptors. |
| `ActionSpec` | immutable descriptor | Physical port, targets, resources, delay, duration and readout/pulse parameters. |

[C++ API](../api.md#controlhpp).

## Reset and errors

Profile validation rejects malformed or duplicate mappings and invalid timing
parameters. Lookup rejects unknown ports or codewords. An unsupported requested
action fails before producer acceptance.

Session reset preserves the map. Select a new profile before constructing a new
simulator to change these definitions.

## Implementation and tests

Source: [control.cpp](../../src/control.cpp) and [control.hpp](../../include/qsbit/control.hpp).

**CTest:** `control.mapping`, `protocol.readout`.

The tests reject unknown port/codeword pairs, duplicate mappings and
incompatible acquisition/arm targets. They also check that changing a mapping
changes the profile fingerprint.
