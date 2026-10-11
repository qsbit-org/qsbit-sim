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
`-DBUILD_TESTING=ON`.

The presets select the Conan-generated toolchain for that compiler and use
Debug dependencies. Release presets are named `clang-ninja-release` and
`gcc-ninja-release`; their build directories have the same `-release` suffix.
Prepare the corresponding configuration before selecting it.

Use `-B` to select another simulator build directory while retaining the
prepared dependencies, for example `cmake --preset clang-ninja -B build-custom`.
Build that directory with `cmake --build build-custom`. Keep compiler and build
type selections in the profiles and presets so they match the dependency binaries.

## Install the executable

```sh
cmake --install build-clang --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
qsbit-sim --help
```

Add the PATH setting to your shell configuration. The default C++-only build needs
no Python environment at runtime. `--prefix` selects the installation directory;
`CMAKE_INSTALL_BINDIR` and `CMAKE_INSTALL_LIBDIR` control its binary and library
subdirectories. The launcher in `bin/` executes `lib/qsbit-sim/qsbit-sim-native`;
matching player assets are installed in `lib/qsbit-sim/web/`. The launcher uses
`exec`, preserving arguments, exit status, and signals. It resolves symlinks and
works from any current directory. Run-file paths remain relative to their config.

### Install with a Python backend

Prepare the [Python prerequisites](prerequisites.md#optional-python-backends) in
an ordinary environment, then build and install from the repository root:

```sh
uv sync --frozen --group build --extra stim
uv run --frozen --extra stim cmake --preset clang-ninja -DQSBIT_PYTHON_BACKENDS=ON
cmake --build --preset clang-ninja --parallel
cmake --install build-clang --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
uv run --frozen --extra stim qsbit-sim --list-backends
```

Select the required extras from [pyproject.toml](../pyproject.toml). `uv sync`
installs the backend package and its dependencies in the project's `.venv`.
`uv run` selects that environment for both build-tree and installed simulators.
With an activated compatible environment, invoke the simulator directly:

```sh
source .venv/bin/activate
qsbit-sim --list-backends
```

CMake installs only the native executable, launcher, web assets, metadata, and
C++ contracts. It does not create a Python environment, copy backend Python code,
or install packages. Installation also works with `DESTDIR`. Users manage Python
packages in their own environment. For another environment, run
`python -m pip install '.[stim]'` from this checkout with that environment activated.

Build-tree and installed commands select `QSBIT_PYTHON` when explicitly set,
then `VIRTUAL_ENV/bin/python`. Without either, build-tree commands use their
configured interpreter; installed commands search PATH for `pythonX.Y`, matching
the bridge's build-time Python major/minor version. Interpreter symlinks retain
the environment path. Keep the linked Python library and native dependencies
available. The selected environment must use the matching Python major/minor and
contain `qsbit-sim-backends` and the selected backend's dependencies.

The backend is imported from that environment. There is no simulator-owned copy
or hidden source injection. Python user-site, current-directory and `PYTHONPATH`
imports are disabled; explicit `--python-path` remains available for custom adapters.
Missing interpreters or packages produce configuration errors. The native mock
backend and `--help` do not require a Python environment.

For updates, stop active consumers, update the backend package/dependencies in
the chosen environment, rebuild and reinstall native code, then restart consumers.
Keep the external backend package compatible with the native simulator. Studio
inherits its launch environment and validates it through simulator discovery;
activate the correct environment before starting Studio.

### Installation discovery

`qsbit-sim --installation-info` writes a JSON object to stdout. Schema 1 contains
`version`, `revision`, `installed`, `player_api` (currently 1), absolute `web_root`,
`python_enabled`, and absolute `python`/`backend` paths (null without the bridge).
With the bridge enabled it initializes the selected Python interpreter and imports
only the base `qsbit_backend` package to report its actual location, failing clearly
if that package is missing. It does not run a simulation or import numerical
backends. SystemC's banner may appear on stderr. Build-tree commands report their
source web directory; installed commands locate web assets beside the native binary.

**CTest:** `cli.install` checks installation without Python provisioning, external
environment selection through activation, explicit override and PATH, actual backend
location, missing-package diagnostics, mock execution without Python, player bytes,
symlink/prefix-with-spaces support, and launcher argument, status, PID and signals.

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
| `QSBIT_BUILD_EXAMPLES` | ON | Assemble example programs with GNU RISC-V binutils. |
| `QSBIT_PYTHON_BACKENDS` | OFF | Build the Python bridge using the Python development headers and libraries, and pybind11. |
| `BUILD_TESTING` | OFF | Build core tests and examples. |
| `QSBIT_TEST_AER` | OFF | Register numerical tests for the `aer` extra. |
| `QSBIT_TEST_QUTIP` | OFF | Register numerical tests for the `qutip` extra. |
| `QSBIT_TEST_STIM` | OFF | Register numerical tests for the `stim` extra. |
| `QSBIT_TEST_QEC` | OFF | Register PyMatching configuration tests for the `qec` extra. |
| `QSBIT_TEST_EXPERIMENTS` | OFF | Register full execution and replay checks for the `experiments` extra. |
| `QSBIT_TEST_WEBSITE` | OFF | Register strict website and browser tests; requires testing and the [website tools](website.md). |
| `QSBIT_ISA_REFERENCES` | OFF | Register independent ISA tests using the `verification` dependency group. |
| `QSBIT_ARCH_TEST_SOURCE` | empty | Select a pinned RISC-V architecture-test checkout. |
| `QSBIT_SANITIZERS` | OFF | Enable address and undefined-behavior sanitizers. |
| `QSBIT_COVERAGE` | OFF | Generate gcov coverage in a build separate from sanitizers. |

Numerical backend tests require both `BUILD_TESTING` and `QSBIT_PYTHON_BACKENDS`.
With both enabled, `python.plugin` also tests an external adapter without
numerical dependencies. An explicitly enabled test fails if its dependencies
are missing.

For a simulator-only build, set `-DQSBIT_BUILD_EXAMPLES=OFF` and leave
`BUILD_TESTING=OFF`. This configuration does not require RISC-V binutils.

## Conan dependencies

[Prepare Conan dependencies](prerequisites.md#prepare-conan-dependencies)
before configuring. CMake uses the generated toolchain in `.conan/`.
Regenerate it when dependencies or compiler settings change, then use
`cmake --fresh` when replacing an existing toolchain.

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
`-DPython3_EXECUTABLE=/path/to/python` when needed. CMake must find the
development headers and library for that interpreter.

For a locked development environment with Aer, QuTiP and ISA tests:

```sh
uv sync --frozen --extra aer --extra qutip --group verification --group dev
source .venv/bin/activate
cmake --preset clang-ninja -B build-python \
  -DBUILD_TESTING=ON -DQSBIT_PYTHON_BACKENDS=ON \
  -DQSBIT_TEST_AER=ON -DQSBIT_TEST_QUTIP=ON -DQSBIT_ISA_REFERENCES=ON
cmake --build build-python --parallel
ctest --test-dir build-python --output-on-failure
```

For a complete development environment with all backend extras and tool groups:

```sh
uv sync --frozen --all-extras --all-groups
```

Runtime backends and experiment dependencies are extras. Build, development,
verification and documentation tools are dependency groups in
[pyproject.toml](../pyproject.toml). uv installs the versions in [uv.lock](../uv.lock).

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
