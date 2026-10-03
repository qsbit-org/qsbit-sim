# Quantum device simulation

A backend calculates quantum evolution and measurement outcomes.
`ControlElectronics` supplies the physical times and orders all calls that change that state.
All ports in a run share this backend state.

## Connections

- **Input:** reset parameters, active drives over an interval, ideal-gate batches
  and measurement references.
- **Output:** measurement bits and optional statevector or density-matrix inspection.
- **Interface:** `IQuantumBackend`, implemented natively or through `PythonBackend`.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Physical event batch and drives"];
  owner [label="IQuantumBackend and ControlElectronics"];
  state [label="IQuantumBackend\nMockBackend\nPythonBackend"];
  output [label="Evolution and sampled bits and state"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Calls at a physical boundary

`ControlElectronics` calls `evolve(start, end, drives)` with all drives
active over the preceding interval. It then measures ending acquisitions
and applies starting gates. All ports share one backend state.

The mock backend supplies configured bits without quantum evolution.
Aer applies ideal gates, optional thermal relaxation and measurements with state
collapse. The pulse backend integrates constant X, Y and Z drives. Stim applies
Clifford gates and optional gate depolarization. See
[backend setup](../backends.md) for installation and adapter methods.

Backend calls are synchronous and do not advance simulation time. Output
timing and result delivery remain controlled by the simulator.
Before successful completion, the backend evolves through the remaining idle
interval to the stop tick.

Qubit 0 is the least significant statevector bit. Pulse amplitudes use
radians per nanosecond; rotation-gate amplitudes use radians.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `IQuantumBackend` | replaceable interface | Defines validation, reset, evolution, gate application, measurement and state inspection. |
| `MockBackend` | deterministic outcomes | Returns configured measurement bits; no quantum statevector. |
| `PythonBackend` | optional bridge | Calls a selected Python adapter with its validated configuration. Numerical packages are optional. |
| `ControlElectronics::active_` | drive source | Provides the joint drive set for the preceding interval. |

[C++ API](../api.md#backendhpp).

## Reset and errors

Requested events are checked for backend support before timing control acceptance
and again before device mutation. Numerical or adapter failures terminate the
run. An adapter need not support every kind of event or statevector inspection.

Session reset calls the backend with the configured qubit count and seed,
creating the initial state for the new epoch.

## Implementation and tests

Source: [python_backend.cpp](../../src/python_backend.cpp) and [backend.hpp](../../include/qsbit/backend.hpp).

**CTest:** `systemc.bell.normal`, `systemc.pulse.normal`.

These tests run complete device sequences with the mock backend. Optional
`numerical.*` tests check numerical Aer and pulse evolution; `python.plugin` checks
loading and calls to an external adapter.
Optional `numerical.final_state` checks thermal relaxation through the stop tick
after the last device event.
Optional `python.backend_configuration` checks discovery, schema validation and
CLI precheck. With `QSBIT_TEST_AER` or `QSBIT_TEST_STIM`, it also checks numerical
noise behavior, measurement collapse and full-program execution.
