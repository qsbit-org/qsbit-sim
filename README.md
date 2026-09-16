# qsbit-sim

C++20 and SystemC architecture simulator for RV32I programs with quantum-control
extensions. Version 0.1.0 implements a three-stage classical pipeline, QuMA-style
timing and per-port event queues, physical resource scheduling, measurement and
feedback, and live Qiskit Aer and small-system pulse backends.

The simulator loads ELF32 or explicit raw machine code. GNU RISC-V assembler macros
encode the quantum instructions. The CPU model, instruction semantics, SystemC
scheduler and quantum-state backend have separate interfaces. TQEC integration and
distributed synchronization are reserved for later phases.

## Build

Linux prerequisites: Python 3.11 with development headers, a C++20 compiler, Git,
GNU Make, and GNU RISC-V binutils (`riscv64-unknown-elf-as`, `ld`, `objdump`). On Debian
or Ubuntu the assembler package is `binutils-riscv64-unknown-elf`. Tests use the
uncompressed RV32I target and disable relaxation.

Provision pinned dependencies once; subsequent configure and build work offline:

```sh
python3.11 -m venv tmp/venv
tmp/venv/bin/python tools/bootstrap.py --architecture-tests
tmp/venv/bin/cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/tmp/deps/systemc-install" \
  -DPython3_EXECUTABLE="$PWD/tmp/venv/bin/python" \
  -DQSBIT_ISA_REFERENCES=ON \
  -DQSBIT_ARCH_TEST_SOURCE="$PWD/tmp/deps/riscv-arch-test"
tmp/venv/bin/cmake --build build -j 4
tmp/venv/bin/ctest --test-dir build --output-on-failure
```

Keep local dependencies outside version control; add `/tmp/` to `.git/info/exclude`
when using the commands above. A supplied SystemC install must be version 3.0.1,
built as C++20. `-DQSBIT_PYTHON_BACKENDS=OFF` builds without Python embedding;
`-DBUILD_TESTING=OFF` removes test and assembler requirements. No reference simulator
is a source, runtime, build or CI dependency.

## Run

```sh
mkdir -p out
build/qsbit-sim --program build/examples/bell.elf --backend aer \
  --trace out/bell.jsonl --summary out/bell.json --inspect 4096 --inspect 4100
build/qsbit-sim --program build/examples/feedback.elf --backend aer \
  --trace out/feedback.jsonl --summary out/feedback.json --inspect 4096
build/qsbit-sim --program build/examples/pulse.elf --backend pulse \
  --trace out/pulse.jsonl --summary out/pulse.json --inspect 4096
```

The Bell example produces correlated measurement bits; feedback prepares `|11>`;
the pulse example performs an X inversion using Hamiltonian evolution. See
[examples/README.md](examples/README.md) for instruction walkthroughs and timing.
`--help` lists CLI options. Profiles are JSON; generate the full default with
`--dump-default-profile out/profile.json`. Simulation ticks are nanoseconds.

## Design and verification

- [Executable implementation](docs/implementation.md): ownership, numerical profile, limits and backend units.
- [High-level design](docs/high-level-design.md) and [module architecture](docs/module-architecture.md): extensibility and shared protocol.
- [Module contracts](docs/modules/README.md): all logical modules, diagrams, source and test links.
- [ISA and trace interface](docs/interfaces.md): machine encodings, CLI and output schema.
- [Engineering and tests](docs/engineering-and-testing.md): deterministic tests, independent ISA references and CI.
- [Glossary](docs/glossary.md): SystemC, cursor, staging, admission, token and timing terminology.

External CACTUS acceptance compares independently executed workloads at supported
quantum boundaries with zero tick tolerance. Original and corrected reference variants
are distinguished; reference defects and unavailable probes are documented in
[ADR 0002](docs/decisions/0002-reference-comparison-scope.md). All comparison tools and
artifacts live in a separate disposable project. Removing it leaves this repository
unchanged. Timing claims do not imply identical classical pipelines or reference
termination behavior.
