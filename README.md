# qsbit-sim

A C++20 and SystemC simulator for RV32I programs with quantum-control extensions.
It models CPU execution, timing-control queues, measurement, and feedback at cycle
level. Quantum-state backends are selected independently of the control model.

## Build

Requires a C++20 compiler, Ninja, and CMake 3.24 or newer. The default example
below selects Clang on Linux. See [install build prerequisites](docs/prerequisites.md)
for installation commands and checks:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel
```

The executable is `build/qsbit-sim`. For GCC, configure a fresh build directory
with `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` instead. Separate
`clang-ninja` and `gcc-ninja` presets are also available for compiler selection
and testing. CMake uses an installed C++20 SystemC library when available;
otherwise it downloads and builds a fixed revision during the
first configure. Python is optional and is not required for this build.
See [build options](docs/building.md) for tests, offline builds, and external SystemC installations.

## Run an example

Install [GNU RISC-V binutils](docs/prerequisites.md#ubuntu)
(`binutils-riscv64-unknown-elf` on Ubuntu), then:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ -DQSBIT_BUILD_EXAMPLES=ON
cmake --build build --parallel
build/qsbit-sim --config build/examples/runs/scripted.json
```

This runs the feedback program with a scripted measurement result. It exercises
the control pipeline without a quantum-state library. Results are written to
`build/runs/scripted.json`; timestamped events are in `build/runs/scripted.jsonl`.
The summary should contain `"success": true` and memory word `"4096": 1`.

## Optional quantum backends

Create and activate a local virtual environment, then install the backend you need:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[aer]'
```

Alternatively, use uv:

```sh
uv sync --extra aer
source .venv/bin/activate
```

Both workflows use `.venv/`. Dependency requirements live in
[pyproject.toml](pyproject.toml); uv uses [uv.lock](uv.lock) for reproducible installs.
Choose the `pulse` extra instead of `aer` for the bundled pulse backend, or install
`.` without extras for the Python bridge and your own adapter.

With the environment active and Python development headers available:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DQSBIT_BUILD_EXAMPLES=ON -DQSBIT_PYTHON_BACKENDS=ON
cmake --build build --parallel
build/qsbit-sim --config build/examples/runs/bell.json
```

CMake discovers the active environment. The Bell example uses Aer and produces
matching measurement bits at addresses 4096 and 4100 in `build/runs/bell.json`.
[Other examples](examples/README.md) cover feedback and overlapping pulse drives.

An external Python adapter can be selected as `"backend": "my_package:MyBackend"`
in the run file. Native C++ adapters implement `IQuantumBackend` and are supplied
to `Simulator`. See [backend integration](docs/backends.md) for both interfaces.

## Configuration

A JSON run file selects the program, backend, output files, and optional timing
profile. Relative paths are resolved from the configuration file's directory.
With examples enabled, CMake generates run files in the selected build tree.
For example, the [Bell run template](examples/runs/bell.json.in) runs with:

```sh
build/qsbit-sim --config build/examples/runs/bell.json
```

Append `--seed 42` to override the seed for one run. Use
`--dump-default-profile build/profile.json` to inspect all hardware parameters.
The [configuration reference](docs/interfaces.md#cli-and-profile) lists fields,
units, precedence, and output formats. `build/qsbit-sim --help` lists CLI options.

## Test

The core tests need Python for test scripts and GNU RISC-V binutils. They do not
need Python quantum backends. See [test prerequisites](docs/prerequisites.md).

```sh
cmake --preset clang-ninja -DBUILD_TESTING=ON
cmake --build --preset clang-ninja --parallel
ctest --preset clang-ninja
```

Backend tests are enabled separately; see [build and test options](docs/building.md).

## Design

- [Module architecture](docs/module-architecture.md) and [module contracts](docs/modules/README.md)
- [Executable implementation](docs/implementation.md)
- [Program and trace interfaces](docs/interfaces.md)
- [Verification](docs/engineering-and-testing.md) and [CACTUS comparison scope](docs/decisions/0002-reference-comparison-scope.md)
- [Glossary](docs/glossary.md)

## Documentation website

The local website includes a clickable architecture, module objects and behavior,
C++ API references, and a player for recorded simulator executions. See
[build and preview the website](docs/website.md).
