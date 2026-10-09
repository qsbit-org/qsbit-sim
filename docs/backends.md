# Use a quantum backend

A backend supplies quantum-state evolution, measurement outcomes, or both.
The mock backend supplies outcomes without maintaining quantum state.
Changing the backend does not change controller-side CPU, TCU, acquisition
duration, or result-delivery timing configured in the profile; a backend may
model additional readout physics internally.

| Backend | Use | Installation |
| --- | --- | --- |
| `mock` | Return configured measurement bits; omitted measurement IDs return zero. No quantum state. | Included in the C++ build. |
| `aer` | Apply ideal gates, optional thermal relaxation and measurements with collapse. | Python bridge and `aer` extra. |
| `qutip` | Evolve driven oscillator models with couplings, dissipation and measurement. | Python bridge and `qutip` extra. |
| `stim` | Apply Clifford gates, optional gate depolarization and measurements with collapse. | Python bridge and `stim` extra. |
| Registered name | Load an installed adapter through its package entry point. | Python bridge and that adapter's dependencies. |
| `module:Class` | Load your own Python adapter. | Python bridge and that adapter's dependencies. |

## Install an optional backend

Install the [Python development prerequisites](prerequisites.md#optional-python-backends), then
choose venv or uv. Run from the repository root.

With venv and pip:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install '.[aer]'
```

With uv:

```sh
uv sync --frozen --group build --extra aer
source .venv/bin/activate
```

Use the `qutip` or `stim` extra for those adapters. To install only the base
Python package, without a numerical backend extra, use `python -m pip install .` or
`uv sync --frozen --group build`.
Dependencies and compatibility ranges are defined in
[pyproject.toml](../pyproject.toml).

With the environment active, build the bridge and run the Bell example:

```sh
cmake --preset clang-ninja -B build-python \
  -DQSBIT_PYTHON_BACKENDS=ON
cmake --build build-python --parallel
cmake --install build-python --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
qsbit-sim --config build-python/examples/bell-state/run.json
```

The simulated Bell measurements should agree: either 00 or 11. The summary and
trace are written under `build-python/examples/bell-state/`. See the
[measurement-feedback example](../examples/measurement-feedback/README.md)
and [QuTiP pulse examples](../examples/qutip/README.md) for other experiments.

## Python runtime

The Python support package is installed by `uv sync` or `pip install .`.
The executable imports it from the runtime environment, without adding the
source checkout to Python's module search path.

Interpreter selection uses `QSBIT_PYTHON` when set, then the active environment's
`VIRTUAL_ENV/bin/python`, then `pythonX.Y` on PATH, where X.Y is the Python
version used to build the bridge. Runtime environments must use that same
Python major and minor version. The linked Python library must remain available.

From this repository, `uv run` selects the project environment:

```sh
uv run --frozen --extra aer qsbit-sim --config build-python/examples/bell-state/run.json
```

To use a separate environment, install the support package and backend there
without editable mode, then select its interpreter:

```sh
uv pip install --python /path/to/environment/bin/python '.[aer]'
export QSBIT_PYTHON=/path/to/environment/bin/python
qsbit-sim --config /path/to/experiment/run.json
```

The package installation command runs from the source checkout. Subsequent runs
need neither that checkout nor its build environment. `--python-path` adds a
directory for custom adapters; it does not select an interpreter.

## Discover and configure backends

With a Python-enabled build, inspect backends without installing their numerical
dependencies:

```sh
qsbit-sim --list-backends
qsbit-sim --backend stim --help-backend
qsbit-sim --backend stim --generate-config > stim.json
qsbit-sim --backend-schema > backend.schema.json
```

These commands write JSON to standard output. Help includes supported operations,
configuration fields, defaults, limits, missing dependencies and an installation
command. The generated configuration uses no noise.
Required adapter parameters without defaults appear as `null`
and must be filled in.

Add `"$schema": "backend.schema.json"` to the run file to enable editor completion.
The exported schema selects `backend_options` according to `backend`. Export it
again after installing or updating adapters. For a direct `module:Class` adapter,
select it with `--backend` when exporting the schema.

Before checking or running `stim.json`, replace its `program` value with the
path to an existing ELF or raw program image, relative to `stim.json`.

```sh
qsbit-sim --config stim.json --check-config
qsbit-sim --config stim.json
```

`--check-config` loads the program, validates the profile and backend options, checks
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

QuTiP configures oscillator levels, Hamiltonian parameters, waveforms, relaxation,
dephasing and readout under `backend_options`. See [QuTiP pulse models](qutip.md).

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
gate application at one device boundary and joint measurement counts as one record.
Gates starting together stay in one application record. The value must be an integer
from 1 to 4294967295; 1 executes each operation immediately. The summary records
the resolved setting. Reset discards pending operations and initializes the
backend for the new epoch.

Each backend executes the complete batch before returning. Aer builds one circuit
per batch, preserving gate, noise and measurement order. QuTiP integrates each
evolution interval with its active drives. Stim updates its persistent tableau.

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

The loader imports `Backend` from `my_adapter` and constructs it with validated
`backend_options`.
`python_path` is optional for an installed package. Program, profile, trace,
summary and `python_path` paths resolve relative to the run file.

Only the selected adapter is imported. The base Python support package and C++
bridge do not require a numerical backend package.
Missing dependencies and malformed adapters fail at construction;
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
| `api_version` | Integer `4`. |
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

A direct `module:Class` adapter must expose the same descriptor through a static
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
| `transition_probabilities(operations)` | Optional. Return the next Z-measurement probability of one for initial states zero and one. Requires a single qubit, time-independent, memoryless noise parameters and ideal computational-basis projective measurement. |

Operation records are defined in [protocol.py](../python/qsbit_backend/protocol.py).
Each record has `kind`, `tick` and fields specific to its kind:

| Kind | Fields | Behavior |
| --- | --- | --- |
| `evolve` | `start`, `drives`, `acquisitions` | Evolve jointly under the active drives from `start` through `tick`, with the active acquisition intervals. |
| `apply` | `gates` | Apply gates at `tick` in input order. |
| `measure` | `references` | Measure targets jointly at `tick` and retain the collapsed state. |

Times are integer nanoseconds and never decrease within an epoch. A measurement
can appear only at the end of a batch. Its references belong to the supplied
epoch and name distinct targets. The simulator owns batch formation and bounds
each batch by `backend_execution.max_batch_operations`.

Gate dictionaries contain `kind`, `operation`, `targets` and `amplitude`.
Drives also contain `port`, `axis`, event `id`, and the complete pulse `start` and
`end`. Acquisition dictionaries contain `kind`, `operation`, `targets`, `port`,
event `id`, complete acquisition `start` and `end`, and `measurement` ID.
These intervals remain unchanged when another event splits an evolution interval.
Measurement references contain
`epoch`, `measurement` and `target`.
Qubit 0 is the least significant statevector bit: basis index 1 represents
qubit 0 set to one and all other qubits set to zero.
Density matrices use the same basis order. QuTiP extends it to configured
oscillator levels using [mixed-radix indices](qutip.md#state-output-and-reproducibility).
Missing inspection methods produce no
state output. Stim does not export a dense quantum state. The summary retains an
empty `statevector` array when unavailable and adds `density_matrix` when supplied;
each complex value is encoded as `[real, imaginary]`.
The simulator executes pending operations before state inspection, including
the final evolution through the simulation stop tick.
Transition operations use the same format with `evolve` and `apply` kinds.
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
