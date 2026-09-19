# Platform Configuration and Clock Adapter

`Simulator` connects the CPU, memory, TCU and device models to the SystemC
scheduler. It creates their clocks, calls each model at the right time, and
applies session resets.

The simulation profile fixes the modeled hardware: clock periods and phases,
communication delays, queue capacities, and port mappings. `Simulator` validates
and copies it before simulation starts. The copy remains unchanged during the run,
including across resets. A run configuration separately selects the executable
program, backend and output paths. See [configuration terms](../glossary.md#run-configuration).

## Connections

- **Input:** a `Profile`, loaded `ProgramImage`, backend, and optional reset ticks
  from the application.
- **Output:** calls to the CPU, memory and TCU models at their rising edges, and
  calls to `DeviceRuntime` at scheduled physical boundaries.
- **Scheduling:** `Simulator` is the SystemC module. The models it calls are C++
  objects with persistent state.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Validated profile and reset request"];
  owner [label="Simulator"];
  state [label="profile_\ncpu_clock_, tcu_clock_\nwake_, barrier_"];
  output [label="Clock edges, reset and physical wakeup"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Clocks and physical boundaries

The CPU and memory methods run on CPU rising edges. The TCU method runs on
TCU rising edges. These processes use `dont_initialize()`, so their first execution
comes from a triggering event rather than an automatic initialization call.
A clock event at time zero can still invoke them. [Process initialization](../glossary.md#initialization)
and [sensitivity](../glossary.md#sensitivity) describe these rules.

Each method checks for reset, advances its model if no reset applies, and records
that it has finished work for the current tick. It then requests the device
barrier with `notify(SC_ZERO_TIME)`, which schedules a later delta at the same
simulation time. It does not call the barrier inline or advance a hardware cycle.

The barrier returns without processing devices until every clocked method due
at this tick has finished. It then processes the physical boundary batch.
It does not suspend with `wait()`. This allows a TCU
launch with zero output delay to join the complete device batch before quantum
state changes.

`schedule_wakeup()` selects the earliest pending device boundary, reset or
watchdog tick. It cancels the previous timed notification before scheduling the
next one. Event payloads remain in the model objects; `sc_event` only wakes a
process. See [event ordering](../module-architecture.md#crossing-and-tcu-edge-order)
for communication between models.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `profile_` | `const Profile` | Validated clocks, capacities, mappings and delays; immutable across reset. |
| `cpu_clock_, tcu_clock_` | `sc_clock` | CPU/memory and TCU rising-edge activation. |
| `wake_, barrier_` | `sc_event` | Timed wakeup and zero-time barrier activation; payload lives in owners. |
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
factories that return no model. Version 1 requires a SystemC resolution of 1 ns.

## Implementation and tests

Source: [simulator.cpp](../../src/simulator.cpp) and [simulator.hpp](../../include/qsbit/simulator.hpp).

**CTest:** `systemc.use_cases`.

The integration tests cover unequal clock periods and phases, reset at coincident
physical boundaries, and exact trace equality after reversing process registration.
