# Build and test options

Run commands from the repository root. CMake writes executables, assembled
examples and test outputs into the selected build directory.
[Quickstart](quickstart.md) covers the first example;
[prerequisites](prerequisites.md) lists the required tools.

## Choose a compiler

The presets keep GCC and Clang builds in separate directories. Choose one:

```sh
cmake --preset gcc-ninja
cmake --build --preset gcc-ninja --parallel
```

```sh
cmake --preset clang-ninja
cmake --build --preset clang-ninja --parallel
```

The executables are `build-gcc/qsbit-sim` and `build-clang/qsbit-sim`.
Add configuration options to the first command, such as
`-DQSBIT_BUILD_EXAMPLES=ON`.

The presets select the Conan-generated toolchain for that compiler and use
Debug dependencies. Release presets are named `clang-ninja-release` and
`gcc-ninja-release`; their build directories have the same `-release` suffix.
Prepare the corresponding configuration before selecting it.

Use `-B` to select another simulator build directory while retaining the
prepared dependencies, for example `cmake --preset clang-ninja -B build-custom`.
Build that directory with `cmake --build build-custom`. Keep compiler and build
type selections in the profiles and presets so they match the dependency binaries.

## Enable tests

```sh
cmake --preset gcc-ninja -DBUILD_TESTING=ON
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

## Conan dependencies

[Prepare dependencies](prerequisites.md#prepare-conan-dependencies) once before
configuring. The project uses the packages selected by Conan; standalone system
installations and the CMake user package registry are not searched. No
`CMAKE_PREFIX_PATH` or shell activation is needed for CMake.

The manifest and lockfile control dependency versions. Generated toolchains
live in `.conan/`, independently of build directories. Deleting a simulator
build directory does not delete the dependency setup. Regenerate the Conan
files when dependencies or compiler configurations change, then configure with
`--fresh` when replacing an existing toolchain.

To update a dependency deliberately, edit [conanfile.py](../conanfile.py),
regenerate the lockfile, and prepare and test each supported configuration:

```sh
uv run --frozen --group build --inexact conan lock create . \
  -pr:a=conan/profiles/clang --lockfile="" --lockfile-out=conan.lock
```

Commit the manifest and lockfile together. Routine installs use the checked-in
lockfile automatically and do not update recipe revisions.

## Python environments

Create optional Python environments in `.venv` and activate them before CMake
configuration. Select an existing interpreter with
`-DPython3_EXECUTABLE=/path/to/python` when needed. Its development library must
match that interpreter.

For a locked development environment with both numerical backends and ISA tests:

```sh
uv sync --frozen --extra pulse --extra verification --extra dev
source .venv/bin/activate
cmake --preset clang-ninja -B build-python \
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
cmake --preset clang-ninja -B build-asan \
  -DBUILD_TESTING=ON -DQSBIT_SANITIZERS=ON
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan --output-on-failure
```

Leak detection is disabled for the SystemC process lifecycle. Address and
undefined-behavior failures remain fatal.
