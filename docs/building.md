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

Run these commands from the simulator source checkout after provisioning the
[build prerequisites](prerequisites.md). Choose the final prefix before creating
the runtime venv. This example explicitly enables Stim; select the desired extras
from [pyproject.toml](../pyproject.toml) when exporting dependencies.

```sh
qsbit_prefix="$HOME/.local"
mkdir -p build
uv export --frozen --extra stim --no-dev --no-emit-project \
  --format requirements-txt > build/runtime-requirements.txt
uv venv --python .venv/bin/python "$qsbit_prefix/lib/qsbit-sim/venv"
uv pip sync --python "$qsbit_prefix/lib/qsbit-sim/venv/bin/python" build/runtime-requirements.txt
uv pip install --python "$qsbit_prefix/lib/qsbit-sim/venv/bin/python" --no-deps .
cmake --preset clang-ninja -DQSBIT_PYTHON_BACKENDS=ON \
  -DPython3_EXECUTABLE="$qsbit_prefix/lib/qsbit-sim/venv/bin/python"
cmake --build --preset clang-ninja --parallel
cmake --install build-clang --prefix "$qsbit_prefix"
export PATH="$qsbit_prefix/bin:$PATH"
qsbit-sim --installation-info
qsbit-sim --list-backends
```

Use the configured library subdirectory instead of `lib` if overriding
`CMAKE_INSTALL_LIBDIR`. Package provisioning is explicit and may access the
network; normal CMake builds and simulator launches never install dependencies.
The install step rejects a missing environment, mismatched Python major/minor
version, editable backend, or backend source files differing from this checkout.
It checks before replacing the native executable. The native bridge loads the
dedicated venv and verifies the imported backend location. Outer Python variables,
user site packages, and the current directory cannot select another backend.

The prefix is fixed: do not move its venv after installation or use `DESTDIR` for
a Python-enabled runtime. Keep the base Python installation, linked Python library,
and other native shared libraries (including SystemC) available. Source and build
trees are not runtime resources. This is not a self-contained CPython distribution.

For upgrades, stop applications and simulations using the prefix, export the new
checkout's locked dependencies with all desired extras, sync the dedicated venv,
install the new checkout non-editably, then rebuild and reinstall. Restart Studio
or other consumers to refresh their discovery metadata. `uv pip sync` removes
packages not in the selected export, so retain all required extras. For a Python
major/minor change, recreate the venv and reconfigure/rebuild against it. Installation
is not an atomic hot upgrade; recover interrupted updates by completing these steps.

### Installation discovery

`qsbit-sim --installation-info` writes a single JSON object to stdout without
starting a simulation or importing numerical backends. Schema 1 contains `version`,
`revision`, `installed`, `player_api` (currently 1), absolute `web_root`,
`python_enabled`, and absolute `python`/`backend` paths (null without the bridge).
SystemC's banner may appear on stderr. Build-tree commands report their configured
source resources with `installed: false`. Installed commands discover resources
relative to the native executable and reject missing Python runtime files.
Executable discovery uses Linux procfs or macOS dyld and resolves symbolic links
before locating resources; it does not depend on the launch directory.

**CTest:** `cli.install` verifies discovery, installed player bytes, execution from
an unrelated directory through a symlink and a prefix containing spaces, and
launcher argument, exit-status, PID and termination-signal preservation. With the
bridge enabled it rejects stale backend installation, checks polluted-environment
isolation for both launcher and native entry point, and checks missing runtime files.

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
