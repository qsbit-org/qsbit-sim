# Quantum device simulation

A backend supplies quantum-state evolution, measurement outcomes, or both.
`ControlElectronics` supplies physical times and commits operations in order.
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
  owner [label="ControlElectronics → BackendExecution → IQuantumBackend"];
  state [label="IQuantumBackend\nMockBackend\nPythonBackend"];
  output [label="Evolution and sampled bits and state"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state and configuration"];
}
```

## Calls at a physical boundary

`ControlElectronics` commits the preceding evolution interval to `BackendExecution`
with all active drives. It then measures ending acquisitions and commits starting
gates. All ports share one backend state.

The mock backend supplies configured bits without quantum-state evolution.
Aer applies ideal gates, optional thermal relaxation and measurements with state
collapse. QuTiP integrates time-dependent oscillator Hamiltonians and dissipation. Stim applies
Clifford gates and optional gate depolarization. See
[backend setup](../backends.md) for installation and adapter methods.

`BackendExecution` collects operations across device boundaries. Measurement,
state inspection and the configured batch limit execute pending work. Each backend
receives the complete ordered batch through `execute()`.

Backend calls are synchronous and do not advance simulation time. Output
timing and result delivery remain controlled by the simulator.
Before successful completion, the simulator commits the remaining idle interval
through the stop tick and executes the final batch.

Qubit 0 is the least significant statevector bit. Pulse amplitudes use
radians per nanosecond; rotation-gate amplitudes use radians.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `BackendExecution` | bounded operation batch | Owns pending operations and decides when to execute them. |
| `IQuantumBackend` | replaceable interface | Defines validation, reset, batch execution and state inspection. |
| `MockBackend` | deterministic outcomes | Returns configured measurement bits; no quantum statevector. |
| `PythonBackend` | optional bridge | Calls a selected Python adapter with its validated configuration. Numerical packages are optional. |
| `ControlElectronics::active_` | drive source | Provides the joint drive set for the preceding interval. |

[C++ API](../api.md#backendhpp).

## Reset and errors

Requested events are checked for backend support before timing control acceptance
and again before device mutation. Numerical or adapter failures terminate the
run. An adapter need not support every kind of event or statevector inspection.

Session reset discards pending operations and resets the backend with the configured qubit count and seed,
creating the initial state for the new epoch.

## Implementation and tests

Source: [backend.cpp](../../src/backend.cpp), [device.cpp](../../src/device.cpp),
[python_backend.cpp](../../src/python_backend.cpp) and [backend.hpp](../../include/qsbit/backend.hpp).

**CTest:** `backend.execution`, `systemc.bell.normal`.

`backend.execution` checks batch capacity, operation order, measurement boundaries,
inspection, reset and failures without SystemC. The SystemC tests run complete
device sequences with the mock backend. Optional
`numerical.*` tests check numerical Aer evolution; `python.plugin` checks
loading and unchanged event timing across batch limits with an external adapter.
Optional `numerical.final_state` checks thermal relaxation through the stop tick
after the last device event.
Optional `python.qutip` checks analytic drive evolution, dissipation, measurement
collapse, and pulse and acquisition intervals passed through the native bridge.
Optional `python.backend_configuration` checks discovery, schema validation and
`--check-config` and backend selection in full-program execution.
Optional `python.backend_semantics` checks numerical evolution, noise,
measurement collapse, batch partitioning and validation for each enabled backend.
