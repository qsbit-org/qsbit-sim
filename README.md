# qsbit-sim

qsbit-sim runs RV32I machine-code programs with quantum-control extension instructions. It
models a cycle-level CPU, timing-control queues, physical operations, measurement, and
feedback in C++20 and SystemC. Select Qiskit Aer for ideal gates or the small-system
pulse backend for piecewise-constant Hamiltonian drives.

## Requirements

Build on Linux from the repository root. Install these system tools first:

| Tool | Used for |
| --- | --- |
| C++20 compiler, Git, Python 3.11 with `venv` and development headers | Build SystemC and the simulator; embed Python for the quantum backends. |
| GNU RISC-V binutils: `riscv64-unknown-elf-as`, `riscv64-unknown-elf-ld`, `riscv64-unknown-elf-objdump` | Assemble and link the example RV32I programs and run tests. The Debian/Ubuntu package is `binutils-riscv64-unknown-elf`. |

`tools/bootstrap.py` installs pinned CMake, formatting and Python packages into the
virtual environment, then builds the pinned SystemC 3.0.1 installation under
`tmp/deps/systemc-install`. The Python packages include Qiskit 2.1.2, Qiskit Aer
0.17.2, NumPy, SciPy, and pybind11. The JSON library is vendored in `third_party/`.
Provisioning requires network access; the later CMake configure and build do not.

## Build and test

```sh
python3.11 -m venv tmp/venv
tmp/venv/bin/python tools/bootstrap.py
tmp/venv/bin/cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/tmp/deps/systemc-install" \
  -DPython3_EXECUTABLE="$PWD/tmp/venv/bin/python"
tmp/venv/bin/cmake --build build -j 4
tmp/venv/bin/ctest --test-dir build --output-on-failure
```

The build creates `build/qsbit-sim` and the four ELF programs in `build/examples/`.
All files under `tmp/` and `build/` are ignored by Git. Run
`tmp/venv/bin/python tools/bootstrap.py --architecture-tests` and configure with
`-DQSBIT_ISA_REFERENCES=ON -DQSBIT_ARCH_TEST_SOURCE="$PWD/tmp/deps/riscv-arch-test"`
when the optional independent ISA reference suite is needed.

For a build without Python backends, set `-DQSBIT_PYTHON_BACKENDS=OFF`; the scripted
backend remains available. Set `-DBUILD_TESTING=OFF` to build without the test and
example targets. A supplied SystemC installation must be version
3.0.1.20241015, built with C++20.

## Run an example

Each file in `examples/runs/` specifies the program, backend, output files, and any
profile changes. Paths inside a run file are relative to that file. From the repository
root, run:

```sh
build/qsbit-sim --config examples/runs/bell.json
build/qsbit-sim --config examples/runs/feedback.json
build/qsbit-sim --config examples/runs/pulse.json
build/qsbit-sim --config examples/runs/overlap.json
```

For example, the Bell run writes `build/runs/bell.json` and
`build/runs/bell.jsonl`. The JSON summary includes success, stop time, final
registers, selected memory words, quantum state, and the complete timing profile.
The JSONL trace contains timestamped instruction and quantum events. Inspect the
summary with:

```sh
tmp/venv/bin/python -m json.tool build/runs/bell.json
```

The Bell measurement bits at addresses 4096 and 4100 must match (00 or 11). Feedback
deterministically stores 1 at address 4096; the pulse example also stores 1.
The overlap example drives X and Z on the same qubit simultaneously. See
[the example walkthrough](examples/README.md) for the programs and expected times.

## Run configuration

`--config FILE` accepts JSON for a complete run. For example:

```json
{
  "schema": 1,
  "program": "../../build/examples/bell.elf",
  "backend": "aer",
  "trace": "../../build/runs/bell.jsonl",
  "summary": "../../build/runs/bell.json",
  "inspect": [4096, 4100],
  "profile": {
    "cpu": {"period": 5, "phase": 0},
    "tcu": {"period": 20, "phase": 0},
    "start": 1000
  }
}
```

This example assumes the file is stored in `examples/runs/`. `profile` is an
optional inline timing and mapping overlay; omitted fields retain defaults.
`profile_file` loads a separate JSON overlay relative to the run file, as in
[the overlap run](examples/runs/overlap.json). The run file also accepts
`memory_base`, `memory_size`, `raw_base`, `resets`, `outcomes`, `memory_dump`,
`python_path`, and `reverse_registration`. Unknown keys and invalid values fail
before simulation starts. Output parent directories are created automatically.

The existing CLI remains useful for temporary overrides. Options are applied from
left to right, so a later option wins:

```sh
build/qsbit-sim --config examples/runs/bell.json --seed 42
build/qsbit-sim --program build/examples/bell.elf --backend aer \
  --trace build/runs/custom.jsonl --summary build/runs/custom.json \
  --inspect 4096 --inspect 4100
```

`--profile FILE` applies a JSON timing overlay without changing the program or
output paths. `--dump-default-profile FILE` writes the complete default timing
profile. `build/qsbit-sim --help` lists all CLI options. Time values are integer
nanoseconds. Exit code 0 means the simulation drained successfully; 1 means a
simulation fault; 2 means invalid input, configuration, or setup.

## Design and verification

- [Executable implementation](docs/implementation.md): ownership, numerical profile, and backend limits.
- [Architecture](docs/module-architecture.md) and [module contracts](docs/modules/README.md): timing and component behavior.
- [Program, configuration, and trace interfaces](docs/interfaces.md): instruction encoding and output schema.
- [Engineering and tests](docs/engineering-and-testing.md): validation layers and test commands.
- [Glossary](docs/glossary.md): SystemC and control-timing terms.

External CACTUS comparisons and their scope are recorded in
[ADR 0002](docs/decisions/0002-reference-comparison-scope.md).
