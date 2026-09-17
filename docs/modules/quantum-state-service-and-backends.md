# Quantum-State Service and Backends

A backend calculates quantum evolution and measurement outcomes.
`DeviceRuntime` supplies the physical times and orders all calls that change that state.
All ports in a run share this backend state.

## Connections

- **Input:** reset parameters, active drives over an interval, ideal-gate batches
  and measurement tokens.
- **Output:** evolved state, measurement bits and optional statevector inspection.
- **Interface:** `IQuantumBackend`, implemented natively or through `PythonBackend`.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Physical event batch and drives"];
  owner [label="IQuantumBackend / DeviceRuntime"];
  state [label="IQuantumBackend\nScriptedBackend\nPythonBackend"];
  output [label="Evolution / sampled bits / state"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Calls at a physical boundary

Before changing quantum state, the runtime checks the complete boundary batch.
It calls `evolve(start, end, drives)` with every drive active over the preceding
interval. It then measures ending acquisitions and applies the new ideal gates
in the order defined by the [device protocol](../module-architecture.md#device-batches-and-feedback).
Ports never advance shared quantum state independently.

The scripted backend returns configured measurement bits and has no statevector.
The Aer adapter supports ideal gates and live measurements with collapse.
The pulse adapter additionally evolves jointly under constant X, Y and Z drives.
See [backend integration](../backends.md) for installation and adapter methods.

Calls are synchronous. A slow numerical calculation increases host execution
time but does not change any simulated timestamp. Measurement readiness and
receiver visibility remain device and communication delays.

Qubit 0 is the least significant statevector bit. Pulse amplitudes are angular
frequencies in radians per nanosecond; rotation-gate amplitudes are angles
in radians.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `IQuantumBackend` | replaceable interface | Defines validation, reset, evolution, gate application, measurement and state inspection. |
| `ScriptedBackend` | deterministic outcomes | Returns configured measurement bits; no quantum statevector. |
| `PythonBackend` | optional bridge | Calls a selected Python adapter; Aer/pulse packages are optional. |
| `DeviceRuntime::active_` | drive source | Provides the joint drive set for the preceding interval. |

[C++ API](../api.md#backendhpp).

## Reset and errors

Requested actions are checked for backend support before producer acceptance
and again before device mutation. Numerical or adapter failures terminate the
run. An adapter need not support every kind of action or statevector inspection.

Session reset calls the backend with the configured qubit count and seed,
creating the initial state for the new epoch.

## Implementation and tests

Source: [python_backend.cpp](../../src/python_backend.cpp) and [backend.hpp](../../include/qsbit/backend.hpp).

**CTest:** `systemc.bell.normal`, `systemc.pulse.normal`.

These tests run complete device sequences with the scripted backend. Optional
`numerical.*` tests check live Aer and pulse evolution; `python.plugin` checks
loading and calls to an external adapter.
