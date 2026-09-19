# Use a quantum backend

The backend evolves quantum state and returns measurement outcomes. The controller
sets the simulated timing. Changing backend does not change CPU, TCU or
readout delays.

| Backend | Use | Installation |
| --- | --- | --- |
| `scripted` | Return fixed measurement bits for control tests; no quantum state. | Included in the C++ build. |
| `aer` | Apply ideal gates and perform state-derived measurements with collapse. | Python bridge and `aer` extra. |
| `pulse` | Evolve constant Hamiltonian drives jointly, with Aer gates and measurements. | Python bridge and `pulse` extra. |
| `package.module:Class` | Load your own Python adapter. | Python bridge and that adapter's dependencies. |

## Install an optional backend

Install the [Python development prerequisites](prerequisites.md#ubuntu), then
choose venv or uv. Run from the repository root.

With venv and pip:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[aer]'
```

With uv:

```sh
uv sync --frozen --group build --extra aer
source .venv/bin/activate
```

Use the `pulse` extra for the bundled pulse adapter. To install only the
bridge package, omit the extra: `python -m pip install -e .` or
`uv sync --frozen --group build`.
Dependencies and compatibility ranges are defined in
[pyproject.toml](../pyproject.toml).

With the environment active, build the bridge and run the Bell example:

```sh
cmake --preset clang-ninja -B build-python \
  -DQSBIT_PYTHON_BACKENDS=ON
cmake --build build-python --parallel
build-python/qsbit-sim --config build-python/examples/runs/bell.json
```

The simulated Bell measurements should agree: either 00 or 11. The summary and
trace are written under `build-python/runs/`. See the
[examples](../examples/README.md) for feedback, pulses and overlapping drives.

## Select a custom Python adapter

Install your adapter in the active environment or supply a local module path
in the run configuration:

```json
{
  "schema": 1,
  "program": "program.elf",
  "backend": "my_adapter:Backend",
  "python_path": "adapters",
  "trace": "results/trace.jsonl",
  "summary": "results/summary.json"
}
```

The loader imports `Backend` from `my_adapter` and constructs it with no arguments.
`python_path` is optional for an installed package. All configured paths resolve
relative to the run file.

Only the selected adapter is imported. The bridge alone requires no Aer or pulse
packages. Missing dependencies and malformed adapters fail at construction;
unsupported requested operations fail validation.

## Python adapter methods

`IQuantumBackend` is the C++ execution interface. `PythonBackend` implements it
as a bridge to the selected Python quantum backend. A CPU adapter implements
the separate `ICpuCycleModel` interface.

| Method | Required behavior |
| --- | --- |
| `validate(action)` | Check kind, operation and targets without changing quantum state. |
| `reset(qubits, seed)` | Initialize state and random sampling for a new epoch. |
| `evolve(start, end, drives)` | Evolve jointly under all active drives over the interval, in nanoseconds. |
| `apply(gates)` | Apply the validated ideal-gate batch at one physical boundary. |
| `measure(tokens)` | Measure targets jointly, collapse state and return one boolean per token in input order. |
| `state()` | Return complex statevector amplitudes, or an empty sequence if inspection is unavailable. |

Action dictionaries contain `kind`, `operation`, `targets`, `port`, `amplitude`
and `axis`. Measurement tokens contain `epoch`, `measurement` and `target`.
Qubit 0 is the least significant statevector bit: basis index 1 represents
qubit 0 set to one and all other qubits set to zero. Numerical backend
measurements come from simulated state; they do not access physical hardware.

Methods complete synchronously and use the supplied seed for reproducibility.
They do not call SystemC timing functions. A slow call increases host runtime
without moving a simulated timestamp.

[custom_backend.py](../tests/fixtures/custom_backend.py) shows a dependency-free
protocol fixture. It is useful for checking adapter loading and method calls;
it does not simulate quantum state.

## Native C++ adapters

Implement [IQuantumBackend](../include/qsbit/backend.hpp) and pass a
`std::unique_ptr<IQuantumBackend>` to `Simulator`. The application constructs
the backend and supplies its dependencies. This interface uses plain C++ types.
The CLI's dynamic module loader is specific to Python adapters.

The [implementation reference](implementation.md#backend-limits) lists the
bundled adapters' qubit limits and numerical assumptions.
