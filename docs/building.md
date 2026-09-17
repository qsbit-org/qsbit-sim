# Build and test options

Run commands from the repository root. CMake writes all generated artifacts into
the selected build directory. Linux is covered by CI.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `QSBIT_FETCH_SYSTEMC` | ON | Fetch the pinned SystemC revision if an installation is not found. |
| `QSBIT_BUILD_EXAMPLES` | OFF | Assemble example programs; requires GNU RISC-V binutils. |
| `QSBIT_PYTHON_BACKENDS` | OFF | Build the Python bridge; requires Python development files and pybind11. |
| `BUILD_TESTING` | OFF | Build core tests and examples; requires a Python interpreter and RISC-V binutils. |
| `QSBIT_TEST_AER` | OFF | Register Aer numerical tests; requires the `aer` extra. |
| `QSBIT_TEST_PULSE` | OFF | Register pulse numerical tests; requires the `pulse` extra. |
| `QSBIT_ISA_REFERENCES` | OFF | Register independent ISA tests; requires the `verification` extra. |
| `QSBIT_ARCH_TEST_SOURCE` | empty | Path to the pinned RISC-V architecture-test checkout. |
| `QSBIT_SANITIZERS` | OFF | Enable address and undefined-behavior sanitizers. |
| `QSBIT_COVERAGE` | OFF | Generate gcov coverage; use a separate build from sanitizers. |

Backend test options require both `BUILD_TESTING` and `QSBIT_PYTHON_BACKENDS`.
Enabling the bridge alone registers an external-adapter test that uses no numerical
packages. An explicitly enabled backend test fails if its dependencies are missing.

## SystemC

To use an existing C++20 SystemC installation:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/systemc-install -DQSBIT_FETCH_SYSTEMC=OFF
```

The pinned source revision is recorded in `cmake/SystemC.cmake`. An offline build
can use a local checkout of that revision:

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SYSTEMC=/path/to/systemc-source
```

CMake downloads missing sources only during configure. Rebuilding a configured tree
does not install Python packages or fetch dependencies. Use a separate build directory
when changing the compiler, Python interpreter, or major dependency versions.

## Python environments

Use `.venv` for the optional bridge and adapters. Activate it before configuring
CMake. To select a different existing interpreter explicitly, set
`-DPython3_EXECUTABLE=/path/to/python`. The interpreter and development library must
belong to the same Python installation. Python compatibility and optional dependency
sets are declared in `pyproject.toml`; there is no single required Python minor version.

A locked development environment with both bundled backends and ISA references:

```sh
uv sync --frozen --extra pulse --extra verification --extra dev
source .venv/bin/activate
cmake -S . -B build-python -DBUILD_TESTING=ON -DQSBIT_PYTHON_BACKENDS=ON \
  -DQSBIT_TEST_AER=ON -DQSBIT_TEST_PULSE=ON -DQSBIT_ISA_REFERENCES=ON
cmake --build build-python --parallel
ctest --test-dir build-python --output-on-failure
```

To use pip instead, create and activate `.venv`, then run
`python -m pip install -e '.[pulse,verification,dev]'`. pip resolves the declared
version ranges; uv uses the checked-in lock file. Neither workflow installs optional
backends unless the corresponding extras are selected.

## Independent ISA tests

`QSBIT_ISA_REFERENCES=ON` enables the randomized differential test. For the architecture
test suite, provide a checkout at revision `37e6e0022814d880375e4a310b4a9a10fb9b268a`
and configure `QSBIT_ARCH_TEST_SOURCE` to that directory. See
[verification](engineering-and-testing.md) for supported cases and coverage limits.

## Sanitizers

```sh
cmake -S . -B build-asan -DBUILD_TESTING=ON -DQSBIT_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan --output-on-failure
```

Leak detection is disabled for the SystemC process lifecycle. Address and
undefined-behavior failures remain fatal.
