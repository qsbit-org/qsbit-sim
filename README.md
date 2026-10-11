# qsbit-sim

[![CI](https://github.com/qsbit-org/qsbit-sim/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/qsbit-org/qsbit-sim/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-online-blue)](https://qsbit-org.github.io/qsbit-sim/)
[![License](https://img.shields.io/github/license/qsbit-org/qsbit-sim)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](docs/prerequisites.md)

qsbit-sim models quantum-control hardware: CPU execution, timed outputs,
measurement feedback and synchronization between controllers. It runs RV32I
programs with qsbit control instructions, using C++20 and SystemC.

The project is inspired by [CACTUS](https://github.com/gtaifu/CACTUS), a
quantum-control architecture simulator.

[Documentation](https://qsbit-org.github.io/qsbit-sim/) ·
[Examples](examples/README.md) ·
[Execution player](https://qsbit-org.github.io/qsbit-sim/execution.html)

> [!NOTE]
> qsbit-sim is in early development. Its architecture and APIs may change without
> backward compatibility. [Feedback and bug reports](https://github.com/qsbit-org/qsbit-sim/issues) are welcome.

## Build

Install the [prerequisites](docs/prerequisites.md), prepare the Conan dependencies,
then run from the repository root:

```sh
cmake --preset clang-ninja
cmake --build --preset clang-ninja --parallel
cmake --install build-clang --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

## Run

```sh
qsbit-sim --config build-clang/examples/measurement-feedback/mock.json
python3 tools/replay_trace.py build-clang/examples/measurement-feedback/mock-results.jsonl
```

The program measures qubit 0 and branches on the result to select a gate on
qubit 1. The mock backend supplies bit 1. The summary,
`mock-results.json`, should contain `"success": true` and `"4096": 1` in
`memory`. The second command opens the execution trace in a browser.

For quantum-state evolution, configure [Aer, Stim or QuTiP](docs/backends.md).

## Learn more

- [Quickstart](docs/quickstart.md): run and inspect both feedback branches.
- [Architecture](docs/high-level-design.md): control, timing and feedback.
- [Configuration reference](docs/interfaces.md): instructions, profiles and outputs.
- [Development](docs/engineering-and-testing.md): builds, tests and contribution checks.

## Citation

If you use qsbit-sim in your work, please cite the software repository:

```bibtex
@misc{qsbit2026sim,
  author       = {{qsbit-sim contributors}},
  title        = {{qsbit-sim}: A quantum-control hardware simulator},
  year         = {2026},
  howpublished = {GitHub repository},
  url          = {https://github.com/qsbit-org/qsbit-sim}
}
```

## References

- QuMA: [An Experimental Microarchitecture for a Superconducting Quantum Processor](https://arxiv.org/abs/1708.07677).
- eQASM: [An Executable Quantum Instruction Set Architecture](https://arxiv.org/abs/1808.02449).
- [Distributed-HISQ: A Distributed Quantum Control Architecture](https://arxiv.org/abs/2509.04798).

## License

[Apache License 2.0](LICENSE). Bundled [nlohmann/json](third_party/nlohmann_json/LICENSE.MIT)
uses the MIT license.
