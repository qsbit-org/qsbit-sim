# Quantum backend integration

The control model calls one backend through `IQuantumBackend`. The backend evolves
quantum state and returns measurement results; DeviceRuntime controls simulated time.
Host computation time never advances the hardware timeline.

## Select an adapter

| Selection | Installation | Behavior |
| --- | --- | --- |
| `scripted` | Core C++ build | Returns configured measurement bits; maintains no quantum state. |
| `aer` | Python bridge and `aer` extra | Ideal gates and live measurement using Aer. |
| `pulse` | Python bridge and `pulse` extra | Joint constant Hamiltonian evolution; uses Aer for gates and measurement. |
| `package.module:Class` | Python bridge and the adapter's dependencies | Imports and constructs a custom adapter with no arguments. |

The package imports each bundled adapter only when selected. Installing the bridge
alone does not install Aer, Qiskit, NumPy, or SciPy. Missing dependencies and malformed
adapters fail during construction; unsupported operations fail capability validation.

A run file can select an external adapter and a local module directory:

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

`python_path` is optional for an installed package. It is resolved relative to the run
file, like other configured paths. Python module loading executes trusted adapter code.

## Python contract

The class must provide these callable methods:

| Method | Contract |
| --- | --- |
| `validate(action)` | Reject unsupported kinds, operations, and target arities without mutating quantum state. |
| `reset(qubits, seed)` | Initialize state for a new session. |
| `evolve(start, end, drives)` | Jointly evolve all active drives over the interval in nanoseconds. |
| `apply(gates)` | Apply the validated batch of ideal gates at one physical boundary. |
| `measure(tokens)` | Jointly measure targets, collapse state, and return one boolean per token in input order. |
| `state()` | Return complex amplitudes for inspection, or an empty sequence when statevector inspection is unavailable. |

Action dictionaries contain `kind`, `operation`, `targets`, `port`, `amplitude`, and
`axis`. Measurement tokens contain `epoch`, `measurement`, and `target`. Qubit 0 is
the least significant statevector bit. Methods must complete synchronously, use the
provided seed for reproducibility, and never call SystemC timing functions.

Implementing the contract requires no changes to simulator source or its backend
selection logic. `tests/fixtures/custom_backend.py` is a dependency-free protocol
fixture; it does not simulate quantum state.

## Native C++ contract

Implement the methods in `include/qsbit/backend.hpp` and pass a
`std::unique_ptr<IQuantumBackend>` to `Simulator`. The caller owns backend construction
and dependencies. The native interface uses C++ types without Python or SystemC types.
This is a compiled integration interface; the CLI's runtime module loader is for
Python adapters.
