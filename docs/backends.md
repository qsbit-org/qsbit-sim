# Use a quantum backend

The backend evolves quantum state and returns measurement outcomes. The controller
sets the simulated timing. Changing backend does not change CPU, TCU or
readout delays.

| Backend | Use | Installation |
| --- | --- | --- |
| `mock` | Return configured measurement bits; omitted measurement IDs return zero. No quantum state. | Included in the C++ build. |
| `aer` | Apply ideal gates, optional thermal relaxation and measurements with collapse. | Python bridge and `aer` extra. |
| `pulse` | Evolve constant Hamiltonian drives jointly, with Aer gates and measurements. | Python bridge and `pulse` extra. |
| `stim` | Apply Clifford gates, optional gate depolarization and measurements with collapse. | Python bridge and `stim` extra. |
| Registered name | Load an installed adapter through its package entry point. | Python bridge and that adapter's dependencies. |
| `package.module:Class` | Load your own Python adapter. | Python bridge and that adapter's dependencies. |

## Install an optional backend

Install the [Python development prerequisites](prerequisites.md#optional-python-backends), then
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

Use the `pulse` or `stim` extra for those adapters. To install only the
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

## Discover and configure backends

With a Python-enabled build, inspect backends without installing their numerical
dependencies:

```sh
build-python/qsbit-sim --list-backends
build-python/qsbit-sim --backend stim --help-backend
build-python/qsbit-sim --backend stim --generate-config > stim.json
build-python/qsbit-sim --backend-schema > backend.schema.json
```

These commands write JSON to standard output. Help includes supported operations,
configuration fields, defaults, limits, missing dependencies and an installation
command. The generated configuration uses no noise. Replace `program.elf` with
your program path. Required adapter parameters without defaults appear as `null`
and must be filled in.

Add `"$schema": "backend.schema.json"` to the run file to enable editor completion.
The exported schema selects `backend_options` according to `backend`. Export it
again after installing or updating adapters. For a direct `module:Class` adapter,
select it with `--backend` when exporting the schema.

```sh
build-python/qsbit-sim --config stim.json --check-config
build-python/qsbit-sim --config stim.json
```

Precheck loads the program, validates the profile and backend options, checks
dependencies and initializes the backend with the configured qubit count and seed.
It does not execute instructions or write simulation outputs. Operations selected
by the running program are validated before execution.

Unknown fields, unsupported models and invalid parameter combinations are errors.
The summary records resolved `backend_options`, including defaults; the profile
records the random seed. A core-only build supports `mock` without Python.

## Noise configuration

Aer supports `noise.model` values `none` and `thermal_relaxation`. Thermal
relaxation requires `method: density_matrix` and a nonempty `noise.qubits` array.
Each entry supplies `qubit`, `t1_ns`, `t2_ns` and `excited_state_population`.
No physical parameter has a default. Unlisted qubits have no relaxation.

T1 and T2 are positive, finite time constants in nanoseconds, with T2 at most
twice T1. The equilibrium excited-state probability is between zero and one.
Aer applies its thermal relaxation channel over each elapsed interval, including
acquisition time, before measurements and starting gates. Gate applications are
instantaneous; measurements sample at acquisition end.

Stim supports `noise.model` values `none` and `depolarizing`. The latter requires
`after_gate_probability` between zero and one. After each mapped gate, each target
independently receives X, Y or Z with probability `after_gate_probability / 3`
each. Waiting causes no noise. Stim does not support thermal relaxation, arbitrary
rotations or pulse drives. Its `rx`, `ry` and `rz` gates accept multiples of pi/2
within an absolute tolerance of 1e-12 radians.

The pulse adapter accepts no noise configuration.

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

For a legacy adapter, the loader imports `Backend` from `my_adapter` and constructs
it with no arguments. Legacy adapters accept only empty `backend_options`.
`python_path` is optional for an installed package. All configured paths resolve
relative to the run file.

Only the selected adapter is imported. The bridge alone requires no Aer or pulse
packages. Missing dependencies and malformed adapters fail at construction;
unsupported requested operations fail validation.

## Register an adapter

Declare a Python package entry point:

```toml
[project.entry-points."qsbit_sim.backends"]
my_simulator = "my_package.metadata:describe"
```

The function returns a descriptor with these fields:

| Field | Meaning |
| --- | --- |
| `api_version` | Integer `1`. |
| `factory` | Implementation class as `module:Class`. |
| `options_schema` | JSON Schema Draft 2020-12 for `backend_options`. |
| `requirements` | Python distribution names used to report missing dependencies. |
| `install` | Installation command shown in help and dependency errors. |
| `capabilities` | Supported operations, state outputs and numerical limits. |

Keep the descriptor module independent of optional numerical dependencies. The
registry imports the implementation only when constructing the selected backend.
Names must be unique and cannot replace bundled backends.

The factory receives one dictionary of validated options, with schema defaults
applied. Use `additionalProperties: false` to reject unknown fields. Validate
cross-field constraints in the constructor and register-dependent constraints in
`reset()`. Help and editor schemas use the same descriptor as runtime validation.
The bundled [catalog](../python/qsbit_backend/catalog.py) contains examples.

A direct `module:Class` adapter can expose the same descriptor through a static
`describe()` method. Its module must be importable to inspect configuration.

## Python adapter methods

Implement these methods on the class selected by `module:Class`:

| Method | Required behavior |
| --- | --- |
| `validate(action)` | Check kind, operation and targets without changing quantum state. |
| `reset(qubits, seed)` | Initialize state and random sampling for a new epoch. |
| `evolve(start, end, drives)` | Evolve jointly under all active drives over the interval, in nanoseconds. |
| `apply(gates)` | Apply the validated ideal-gate batch at one physical boundary. |
| `measure(references)` | Measure targets jointly, collapse state and return one boolean per measurement reference in input order. |
| `state()` | Optional. Return complex statevector amplitudes. |
| `density_matrix()` | Optional. Return rows of complex density-matrix entries. |

Event dictionaries contain `kind`, `operation`, `targets`, `port`, `amplitude`
and `axis`. Measurement dictionaries contain `epoch`, `measurement` and `target`.
Qubit 0 is the least significant statevector bit: basis index 1 represents
qubit 0 set to one and all other qubits set to zero.
Density matrices use the same basis order. Missing inspection methods produce no
state output. Stim does not export a dense quantum state. The summary retains an
empty `statevector` array when unavailable and adds `density_matrix` when supplied;
each complex value is encoded as `[real, imaginary]`.
State inspection reports the last device boundary, not subsequent CPU execution time.

Methods complete synchronously and use the supplied seed for reproducibility.
They do not call SystemC timing functions. A slow call increases host runtime
without moving a simulated timestamp.

[custom_backend.py](../tests/fixtures/custom_backend.py) provides a mock adapter
for testing loading and method calls.

## Native C++ adapters

Implement [IQuantumBackend](../include/qsbit/backend.hpp) and pass a
`std::unique_ptr<IQuantumBackend>` to `Simulator`. The application constructs
the backend and supplies its dependencies. This interface uses plain C++ types.
The CLI's dynamic module loader is specific to Python adapters.

The [implementation reference](implementation.md#backend-limits) lists the
bundled adapters' qubit limits and numerical assumptions.
