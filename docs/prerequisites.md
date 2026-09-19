# Install build prerequisites

Choose the tools for the workflow you need. A normal simulator build requires
CMake, a C++20 toolchain, Conan-managed SystemC, and GNU RISC-V binutils
for the included examples. Tests also require a Python interpreter.

| Workflow | Required tools |
| --- | --- |
| Build the simulator | CMake 3.24 or newer, Ninja, a C++20 Clang or GCC toolchain, GNU RISC-V assembler and linker, and dependencies prepared with Conan 2. Preparation requires Python and uv. |
| Run core tests | Build tools, RISC-V objdump and Python. |
| Use Python backends | Build tools, Python, matching development files, and a local `.venv` with the selected adapter. |
| Build the website | Build tools, Python documentation dependencies, Doxygen and Graphviz. See [website setup](website.md). |

A default C++ build does not require Python quantum packages.

## Ubuntu

CI uses Ubuntu 24.04. For the default build with its bundled examples, install:

```sh
sudo apt-get update
sudo apt-get install -y gcc g++ clang git cmake ninja-build python3-venv binutils-riscv64-unknown-elf
```

For Clang, install `clang` and select the `clang-ninja` preset. Use the same
compiler family to build SystemC and the simulator.

The RISC-V tools use the `riscv64-unknown-elf-` prefix but can produce the
RV32I ELF files used here. Building these examples does not require a RISC-V
C compiler.

Install Python if you will run tests:

```sh
sudo apt-get install -y python3
```

For Python backends, also install venv support and the matching development files:

```sh
sudo apt-get install -y python3-venv python3-dev
```

Then follow [backend installation](backends.md#install-an-optional-backend).
Python package constraints are in [pyproject.toml](../pyproject.toml);
[uv.lock](../uv.lock) records reproducible resolutions.

## Prepare Conan dependencies

Run these commands from the repository root after installing
[uv](https://docs.astral.sh/uv/getting-started/installation/):

```sh
uv sync --frozen --group build --inexact
uv run --frozen --group build --inexact conan config install conan/config
uv run --frozen --group build --inexact conan install . -pr:a=conan/profiles/clang --build=missing
```

This installs the locked Conan version, applies the project's exact binary
compatibility policy to the Conan user configuration, and prepares SystemC.
The compatibility policy disables reuse across different compiler settings;
SystemC requires the same C++ standard as its consumer. The supplied Linux
profiles detect the selected compiler version and host architecture, select
C++20 and libstdc++, and use matching C, C++, and assembly compilers.
No default Conan profile or SystemC installation prefix is needed.

Conan downloads a matching binary when available; otherwise it compiles the
locked sources during this preparation step. Packages stay in the Conan cache.
Generated CMake files stay in `.conan/`, separately from simulator build trees.
Neither directory belongs in Git. Keep the generated files and cached packages
available during development.

Prepare GCC instead, or in addition to Clang, with:

```sh
uv run --frozen --group build --inexact conan install . -pr:a=conan/profiles/gcc --build=missing
```

The `clang-ninja` and `gcc-ninja` presets use Debug dependencies. To prepare a
Release configuration, add `-s build_type=Release` to its install command and
use `clang-ninja-release` or `gcc-ninja-release` for subsequent CMake commands.
Compiler families and build configurations have separate generated files.

After preparation, normal development only needs CMake:

```sh
cmake --preset clang-ninja
cmake --build --preset clang-ninja --parallel
```

New terminals and IDE sessions do not need an activated Python environment or
`CMAKE_PREFIX_PATH`. Removing `build-clang` or `build-gcc` does not remove the
prepared dependencies. Existing standalone SystemC installations are not used.

Rerun the install command after changing the Conan manifest, lockfile, compiler,
or build configuration, or after removing `.conan/` or Conan's cached packages.
Unchanged dependencies are reused. When switching an existing build to Conan,
or upgrading a compiler, configure once with `cmake --fresh --preset clang-ninja`.

Dependency requirements are in [conanfile.py](../conanfile.py), recipe revisions
are pinned in [conan.lock](../conan.lock), and the Conan tool is locked in
[uv.lock](../uv.lock). The default C++ workflow installs no quantum backends.

## Check the tools

For a GCC example build:

```sh
cmake --version
ninja --version
g++ --version
riscv64-unknown-elf-as --version
riscv64-unknown-elf-ld --version
```

Use `clang++ --version` when selecting Clang. For tests, also check
`python3 --version` and `riscv64-unknown-elf-objdump --version`.
Check the prepared dependency configuration with `cmake --preset clang-ninja`.

## Offline builds

Complete dependency preparation while online. CMake configure, build, and test
commands do not contact Conan remotes. To regenerate `.conan/` from packages
already cached locally, rerun the Conan install command with `--no-remote`.

Continue with [your first simulation](quickstart.md).
