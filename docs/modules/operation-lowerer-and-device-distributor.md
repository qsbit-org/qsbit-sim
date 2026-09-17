# Operation Lowerer and Device Distributor

The lowerer expands one port/codeword command into the device actions defined
by the profile. A command can select several actions on different physical ports,
such as acquisition and a separate discriminator arm.

## Connections

- **Input:** source port and codeword, profile, epoch, instruction ID, first event ID,
  and any measurement or condition token.
- **Output:** `ReservedEvent` values for the producer's open group.
- **Caller:** `TimelineProducer` during APPEND; group validation also runs when
  the producer seals a group and when the TCU checks admission.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Open producer group and codeword map"];
  owner [label="lower / validate_group"];
  state [label="Profile::mappings\nReservedEvent\nGroup"];
  output [label="ReservedEvent values / validated Group"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="value records / configuration"];
}
```

## Resolving and validating actions

`lower()` looks up the mapping and creates one event per `ActionSpec`.
It assigns consecutive event IDs while preserving the source instruction,
source port and codeword. Each event also names its physical output port,
resources, targets and timing parameters.

Acquisition and arm actions carry the measurement token already reserved by the
scoreboard. The producer gathers all returned events into its open group.
When the group is sealed, it assigns a label and builds a manifest containing
the exact event IDs.

`validate_group()` checks those identities, the action mappings, acquisition/arm
pairing and per-port bounds. It validates a complete value before that value is
inserted into TCU queues. Lookup and validation add no separate simulated stage.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `Profile::mappings` | read-only mappings | Maps a source port/codeword to one or more ActionSpec values. |
| `ReservedEvent` | output values | Resolved action plus epoch, event/instruction IDs and optional tokens. |
| `Group` | producer-owned aggregate | Timing point, ordered manifest, events and profile fingerprint. |

[C++ API](../api.md#controlhpp).

## Reset and errors

Unknown mappings, inconsistent identities, invalid acquisition/arm pairs and
impossible group sizes raise typed faults. Producer checks reject staging and
per-port capacity violations before accepting the APPEND.

The lowerer retains no mutable state. Reset discards the producer's generated
events; the profile mappings remain unchanged. Multi-point microcode expansion
is not implemented.

## Implementation and tests

Source: [control.cpp](../../src/control.cpp) and [control.hpp](../../include/qsbit/control.hpp).

**CTest:** `control.mapping`, `protocol.capacity`.

The tests reject invalid mappings and manifests, and groups that exceed
staging or result capacity.
