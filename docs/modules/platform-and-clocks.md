# Platform and clocks

`Simulator` connects each `Core` and the shared device model to the SystemC
scheduler. It calls each model on its clock edges or scheduled device ticks
and applies session resets.

The simulation profile fixes the modeled hardware: clock periods and phases,
communication delays, queue capacities, and port mappings. `Simulator` validates
and copies it before simulation starts. The copy remains unchanged during the run,
including across resets. A run configuration separately selects the executable
program, backend and output paths. See [configuration terms](../glossary.md#run-configuration).

## Connections

- **Input:** a `Profile`, loaded `ProgramImage`, backend, and optional reset ticks
  from the application.
- **Output:** calls to the CPU, memory and TCU models at their rising edges, and
  calls to `ControlElectronics` at scheduled physical boundaries.
- **Scheduling:** `Simulator` is the SystemC module. The models it calls are C++
  objects with persistent state.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Validated profile and reset request"];
  owner [label="Simulator"];
  state [label="profile_\ncpu_clocks_, tcu_clock_\nwake_, barrier_"];
  output [label="Clock edges, reset and physical wakeup"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Clocks and physical boundaries

CPU and memory methods run on CPU rising edges; the TCU method runs on TCU
rising edges. With `dont_initialize()`, each method first runs when its
triggering event occurs, which can be a clock edge at time zero.

Each method checks for reset, advances its model, records completion and
notifies the device barrier for a later delta cycle. The barrier processes
device events only after all clocked methods due at that tick have finished.
This includes events triggered with zero output delay.

The barrier also advances the configured [decoder](../decoding.md) after
the clocked memory accesses. Decoder results therefore become visible to the
CPU on a later memory edge.

`schedule_wakeup()` selects the earliest device event, decoder transition, reset or watchdog
deadline. It cancels the previous timed notification before scheduling
the next one. See [simulation execution](../simulation-model.md) for
same-tick ordering.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `profile_` | `const Profile` | Validated clocks, capacities, mappings and delays; immutable across reset. |
| `cpu_clocks_, tcu_clock_` | per-core CPU clocks and shared TCU clock | Rising edges for each core's CPU, memory and TCU. |
| `wake_, barrier_` | `sc_event` | Schedule timed work and same-tick device processing. |
| `cpu_done_, memory_done_, tcu_done_` | optional tick | Marks which coincident edge transitions have completed. |
| `epoch_, resets_, last_reset_` | session state | Applies a reset once per requested tick and invalidates old work. |

[C++ API](../api.md#simulatorhpp).

## Reset and errors

Every clock method and timed wakeup checks reset before ordinary work.
The first callback at a reset tick applies it; other callbacks at that tick skip
their normal transitions. Reset starts a new epoch and initializes the backend
while preserving memory bytes and the simulation profile. SystemC time keeps moving
forward.

Construction rejects invalid profiles, reset ticks, missing backends and CPU
factories that return no model. Set the SystemC resolution to 1 ns before
constructing `Simulator`. Construction rejects other resolutions before
initializing the backend or creating clocks.

## Implementation and tests

Source: [simulator.cpp](../../src/simulator.cpp) and [simulator.hpp](../../include/qsbit/simulator.hpp).

**CTest:** `systemc.use_cases`, `adapter.fine_resolution`, `adapter.coarse_resolution`.

The integration tests cover unequal clock periods and phases, reset at coincident
physical boundaries, and exact trace equality after reversing process registration.
The resolution tests reject unsupported precision before backend initialization.
