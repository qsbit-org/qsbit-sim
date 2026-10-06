# qsbit-sim

[![CI](https://github.com/qsbit-org/qsbit-sim/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/qsbit-org/qsbit-sim/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-online-blue)](https://qsbit-org.github.io/qsbit-sim/)
[![License](https://img.shields.io/github/license/qsbit-org/qsbit-sim)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](docs/prerequisites.md)

qsbit-sim is a C++20 and SystemC simulator for RV32I programs with
quantum-control instructions. It models CPU cycles, timed control output,
measurement and feedback. Use the built-in mock backend to test control
behavior, or a numerical backend for quantum-state evolution.

## Build

Install the [prerequisites](docs/prerequisites.md) and prepare the Conan
dependencies. Then run from the repository root:

```sh
cmake --preset clang-ninja
cmake --build --preset clang-ninja --parallel
```

This builds `build-clang/qsbit-sim` and the example programs. Use `gcc-ninja`
for GCC after preparing its dependencies. Python quantum packages are optional.
See [build options](docs/building.md) for tests, Release builds and sanitizers.

## Run an example

```sh
build-clang/qsbit-sim --config build-clang/examples/measurement-feedback/mock.json
```

The program measures qubit 0, reads the result and branches to select a gate
on qubit 1. The mock backend supplies result 1. The run writes:

- `build-clang/examples/measurement-feedback/mock-results.json`: final state, with `"success": true` and memory word `"4096": 1`.
- `build-clang/examples/measurement-feedback/mock-results.jsonl`: timestamped execution events.

Replay the trace in your browser:

```sh
python3 tools/replay_trace.py build-clang/examples/measurement-feedback/mock-results.jsonl
```

Press Ctrl+C to stop the local server. The player also reads the matching
summary file when available. [Quickstart](docs/quickstart.md) explains the
program's timing and shows how to test the other branch.

## Quantum backends

To run the Bell example with Qiskit Aer:

```sh
uv sync --frozen --group build --extra aer
source .venv/bin/activate
cmake --preset clang-ninja -DQSBIT_PYTHON_BACKENDS=ON
cmake --build --preset clang-ninja --parallel
build-clang/qsbit-sim --config build-clang/examples/bell-state/run.json
```

The Python bridge requires the [Python development headers and libraries](docs/prerequisites.md#optional-python-backends)
for the interpreter used to build it.
The Bell measurements at addresses 4096 and 4100 should agree.
See [backend setup](docs/backends.md) for installation and configuration, QuTiP pulse models
and custom adapters, and [examples](examples/README.md) for complete programs.

## Test

Core tests require Python and GNU RISC-V binutils:

```sh
cmake --preset clang-ninja -DBUILD_TESTING=ON
cmake --build --preset clang-ninja --parallel
ctest --preset clang-ninja
```

[Testing](docs/engineering-and-testing.md) covers optional numerical tests,
cross-project CI and checks to run before a pull request.

## Documentation

- [Quickstart](docs/quickstart.md)
- [Configuration and trace reference](docs/interfaces.md)
- [Controller architecture](docs/high-level-design.md) and [diagram](docs/architecture.md)
- [Simulation timing](docs/module-architecture.md)
- [Distributed simulation](docs/distributed-simulation.md) and [Distributed-HISQ example](examples/distributed-hisq/README.md)
- [Decoder feedback](docs/decoding.md) and [QEC example](examples/qec/README.md)
- [Component reference](docs/modules/README.md) and [C++ interfaces](docs/cpp-interfaces.md)
- [CACTUS validation](CACTUS_VALIDATION.md)

Read the [documentation website](https://qsbit-org.github.io/qsbit-sim/) or
[build it locally](docs/website.md).

## References

The timing-control model draws on these architectures:

- [QuMA](https://arxiv.org/abs/1708.07677): codeword output and queue-based timing control.
- [eQASM](https://arxiv.org/abs/1808.02449): reserve and trigger phases, operation timing and feedback.
- [Distributed-HISQ](https://arxiv.org/abs/2509.04798): RISC-V control extensions and booking-based neighbor synchronization.

qsbit-sim uses [custom-0 control instructions and custom-1 bundles](docs/interfaces.md#quantum-instruction-encoding).
It does not execute eQASM or HISQ binaries. Timing regression tests compare
selected workloads against [CACTUS](https://github.com/gtaifu/CACTUS).

## License

[Apache License 2.0](LICENSE). Bundled [nlohmann/json](third_party/nlohmann_json/LICENSE.MIT)
uses the MIT license.
