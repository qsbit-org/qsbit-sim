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
build-python/qsbit-sim --config build-python/examples/bell-state/run.json
```

The simulated Bell measurements should agree: either 00 or 11. The summary and
trace are written under `build-python/examples/bell-state/`. See the
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

## Backend execution

The simulator collects time-ordered operations across device boundaries and sends
them to the backend when a measurement needs an outcome. State inspection, the
batch limit and successful completion also execute pending work. Measurements
sample at acquisition end; result delivery keeps its configured delay.

Set the batch limit in the run configuration:

```json
{"backend_execution": {"max_batch_operations": 1024}}
```

The limit applies to every backend and defaults to 1024. Each evolution interval,
gate and joint measurement counts as one operation. The value must be an integer
from 1 to 4294967295; 1 executes each operation immediately. The summary records
the resolved setting. Reset discards pending operations and initializes the
backend for the new epoch.

Each backend executes the complete batch before returning. Aer builds one circuit
per batch, preserving gate, noise and measurement order. The pulse adapter adds
joint drive evolution to that circuit. Stim updates its persistent tableau.

Aer measurement batch n uses `(seed + n) mod 2^32`, starting at n = 0 after reset.
Changing the batch limit or inspecting state does not advance the measurement
seed. Batching changes host execution cost, not simulated timestamps.

Measure backend runtime from the repository root:

```sh
python tools/benchmark_aer.py --output build-clang/aer-benchmark.json
```

The benchmark passes batches of up to 1 and 1024 operations directly to Aer over
repeated runs. It checks identical measurement samples and final states, and
records backend calls, Aer job counts, host runtimes and package versions.
`--backend-options FILE` supplies a JSON options object;
`--measure-every` sets the number of gate events between measurements.
Timing excludes package imports, backend construction, warmup and the C++ simulator.

Stim maintains a persistent stabilizer tableau and updates it directly for
Clifford operations. Select it for large Clifford circuits whose noise fits
the supported depolarizing model. Aer stores a dense statevector or density
matrix and transfers that state at each circuit execution.

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
| `api_version` | Integer `2`. |
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
| `execute(epoch, operations)` | Execute the ordered batch and return measurement bits in reference order, or an empty list when no measurement is present. |
| `state()` | Optional. Return complex statevector amplitudes. |
| `density_matrix()` | Optional. Return rows of complex density-matrix entries. |
| `transition_probabilities(operations)` | Optional. Return the next Z-measurement probability of one for initial states zero and one. Requires a single qubit, stationary memoryless evolution and ideal projective measurement. |

Each operation has `tick`, `method` and positional `args`:

| Method | Arguments | Behavior |
| --- | --- | --- |
| `evolve` | `[start, end, drives]` | Evolve jointly under the active drives over this interval; `tick` equals `end`. |
| `apply` | `[gates]` | Apply gates at `tick` in input order. |
| `measure` | `[references]` | Measure targets jointly at `tick` and retain the collapsed state. |

Times are integer nanoseconds and never decrease within an epoch. A measurement
can appear only at the end of a batch. Its references belong to the supplied
epoch and name distinct targets. The simulator owns batch formation and bounds
each batch by `backend_execution.max_batch_operations`.

Event dictionaries contain `kind`, `operation`, `targets`, `port`, `amplitude`
and `axis`. Measurement dictionaries contain `epoch`, `measurement` and `target`.
Qubit 0 is the least significant statevector bit: basis index 1 represents
qubit 0 set to one and all other qubits set to zero.
Density matrices use the same basis order. Missing inspection methods produce no
state output. Stim does not export a dense quantum state. The summary retains an
empty `statevector` array when unavailable and adds `density_matrix` when supplied;
each complex value is encoded as `[real, imaginary]`.
The simulator executes pending operations before state inspection, including
the final evolution through the simulation stop tick.
Transition operations use the same format with `evolve` and `apply` methods.
Evolution intervals use nanoseconds relative to zero. Transition calculation returns
probabilities without sampling a measurement.

Adapters retain state between calls and use the supplied seed for reproducibility.
They do not call SystemC timing functions. A slow call increases host runtime
without moving a simulated timestamp.

[custom_backend.py](../tests/fixtures/custom_backend.py) provides a mock adapter
for testing loading and method calls.

## Native C++ adapters

Implement [IQuantumBackend](../include/qsbit/backend.hpp) and pass a
`std::unique_ptr<IQuantumBackend>` to `Simulator`. The application constructs
the backend and supplies its dependencies. This interface uses plain C++ types.
The CLI's dynamic module loader is specific to Python adapters.

`ControlElectronics` commits operations to `BackendExecution`, which owns the
pending batch and calls `IQuantumBackend::execute()`. Reset discards the batch.
Execution failures terminate the run and report its epoch and tick range.

The [implementation reference](implementation.md#backend-limits) lists the
bundled adapters' qubit limits and numerical assumptions.
