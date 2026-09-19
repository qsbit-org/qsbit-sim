# Build and test options

Run commands from the repository root. CMake writes executables, assembled
examples and test outputs into the selected build directory.
[Quickstart](quickstart.md) covers the first example;
[prerequisites](prerequisites.md) lists the required tools.

## Choose a compiler

The presets keep GCC and Clang builds in separate directories. Choose one:

```sh
cmake --preset gcc-ninja -DCMAKE_PREFIX_PATH="$HOME/.local/systemc"
cmake --build --preset gcc-ninja --parallel
```

```sh
cmake --preset clang-ninja -DCMAKE_PREFIX_PATH="$HOME/.local/systemc"
cmake --build --preset clang-ninja --parallel
```

The executables are `build-gcc/qsbit-sim` and `build-clang/qsbit-sim`.
Add configuration options to the first command, such as
`-DQSBIT_BUILD_EXAMPLES=ON`.

To select a compiler by path, set `CMAKE_CXX_COMPILER` in a fresh build directory.
Build SystemC separately with the matching C, C++, and assembly compilers.
You can also configure without a preset:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local/systemc"
cmake --build build --parallel
```

That form uses CMake's default generator and compiler. Use a new build directory
when changing compiler, Python interpreter or major dependency versions.

## Enable tests

```sh
cmake --preset gcc-ninja -DCMAKE_PREFIX_PATH="$HOME/.local/systemc" -DBUILD_TESTING=ON
cmake --build --preset gcc-ninja --parallel
ctest --preset gcc-ninja
```

`BUILD_TESTING` also builds the example programs. Core tests need Python and
RISC-V binutils, but no numerical quantum packages. See the
[testing guide](engineering-and-testing.md) for test labels and optional suites.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `QSBIT_BUILD_EXAMPLES` | OFF | Assemble example programs with GNU RISC-V binutils. |
| `QSBIT_PYTHON_BACKENDS` | OFF | Build the Python bridge using Python development files and pybind11. |
| `BUILD_TESTING` | OFF | Build core tests and examples. |
| `QSBIT_TEST_AER` | OFF | Register numerical tests for the `aer` extra. |
| `QSBIT_TEST_PULSE` | OFF | Register numerical tests for the `pulse` extra. |
| `QSBIT_TEST_WEBSITE` | OFF | Register strict website and browser tests; requires testing and the [website tools](website.md). |
| `QSBIT_ISA_REFERENCES` | OFF | Register independent ISA tests using the `verification` extra. |
| `QSBIT_ARCH_TEST_SOURCE` | empty | Select a pinned RISC-V architecture-test checkout. |
| `QSBIT_SANITIZERS` | OFF | Enable address and undefined-behavior sanitizers. |
| `QSBIT_COVERAGE` | OFF | Generate gcov coverage in a build separate from sanitizers. |

Numerical backend tests require both `BUILD_TESTING` and `QSBIT_PYTHON_BACKENDS`.
With both enabled, `python.plugin` also tests an external adapter without
numerical dependencies. An explicitly enabled test fails if its dependencies
are missing.

## SystemC

Install SystemC 3.0.1 with C++20 before configuring the simulator. See the
[installation instructions](prerequisites.md#systemc). For a user-local install:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local/systemc"
```

Use a different install prefix with `-DCMAKE_PREFIX_PATH=/path/to/systemc`.
CMake also searches standard system prefixes such as `/usr/local` without this
option. It does not download dependencies during configuration. The CMake user
package registry is ignored so stale temporary build registrations cannot
override an installed SystemC package.

## Python environments

Create optional Python environments in `.venv` and activate them before CMake
configuration. Select an existing interpreter with
`-DPython3_EXECUTABLE=/path/to/python` when needed. Its development library must
match that interpreter.

For a locked development environment with both numerical backends and ISA tests:

```sh
uv sync --frozen --extra pulse --extra verification --extra dev
source .venv/bin/activate
cmake -S . -B build-python -DCMAKE_PREFIX_PATH="$HOME/.local/systemc" \
  -DBUILD_TESTING=ON -DQSBIT_PYTHON_BACKENDS=ON \
  -DQSBIT_TEST_AER=ON -DQSBIT_TEST_PULSE=ON -DQSBIT_ISA_REFERENCES=ON
cmake --build build-python --parallel
ctest --test-dir build-python --output-on-failure
```

For pip, create and activate `.venv`, then run
`python -m pip install -e '.[pulse,verification,dev]'`. pip resolves the ranges
in [pyproject.toml](../pyproject.toml); uv uses [uv.lock](../uv.lock).
Select only the extras your workflow needs.

## Independent ISA tests

`QSBIT_ISA_REFERENCES=ON` enables randomized comparison with an independent ISA
engine. To include architecture-test bodies, set `QSBIT_ARCH_TEST_SOURCE` to
a checkout at revision `37e6e0022814d880375e4a310b4a9a10fb9b268a`.
The [testing guide](engineering-and-testing.md#independent-isa-checks) explains
coverage and exclusions.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_PREFIX_PATH="$HOME/.local/systemc" \
  -DBUILD_TESTING=ON -DQSBIT_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan --output-on-failure
```

Leak detection is disabled for the SystemC process lifecycle. Address and
undefined-behavior failures remain fatal.
