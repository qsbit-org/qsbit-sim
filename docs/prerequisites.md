# Install build prerequisites

The default build requires CMake, a C++20 compiler, Conan-managed SystemC and
GNU RISC-V binutils for examples. Tests also require a Python interpreter.

| Workflow | Required tools |
| --- | --- |
| Build the simulator | CMake 3.24 or newer, Ninja, a C++20 Clang or GCC toolchain, GNU RISC-V assembler and linker, and dependencies prepared with Conan 2. Preparation requires Python and uv. |
| Run core tests | Build tools, RISC-V objdump and Python. |
| Use Python backends | Build tools, Python, matching development files, and a local `.venv` with the selected adapter. |
| Build the website | Build tools, Python documentation dependencies, Doxygen and Graphviz. See [website setup](website.md). |

A default C++ build does not require Python quantum packages.

## Ubuntu

Install GCC, Clang and the tools for examples and core tests:

```sh
sudo apt-get update
sudo apt-get install -y gcc g++ clang git cmake ninja-build python3 python3-venv binutils-riscv64-unknown-elf
```

The RISC-V tools use the `riscv64-unknown-elf-` prefix but can produce the
RV32I ELF files used here. Building these examples does not require a RISC-V
C compiler.

## macOS

Install [Xcode Command Line Tools](https://developer.apple.com/xcode/resources/)
and [Homebrew](https://brew.sh/) once, then copy this block:

```sh
brew install cmake ninja git python uv riscv64-elf-binutils
```

The `clang-ninja` preset uses Apple Clang. CMake finds Homebrew's
`riscv64-elf-` tools, which support the RV32I examples.

## Prepare Conan dependencies

Run these commands from the repository root after installing
[uv](https://docs.astral.sh/uv/getting-started/installation/):

```sh
uv sync --frozen --group build --inexact
uv run --frozen --group build --inexact conan config install conan/config
uv run --frozen --group build --inexact conan install . -pr:a=conan/profiles/clang --build=missing
```

These commands install the locked Conan version and prepare SystemC with
matching compiler settings and C++20. The profiles detect the compiler version
and host architecture. Linux uses libstdc++; macOS uses libc++.

Conan downloads a matching binary or builds the locked sources. Packages stay
in the Conan cache; generated toolchains stay in `.conan/`. Keep both available
for subsequent builds.

On Ubuntu, prepare GCC instead, or in addition to Clang, with:

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

CMake uses the generated toolchain without shell activation or
`CMAKE_PREFIX_PATH`. Removing a build directory leaves the prepared
dependencies intact.

Rerun the install command after changing the Conan manifest, lockfile, compiler,
or build configuration, or after removing `.conan/` or Conan's cached packages.
Unchanged dependencies are reused. When switching an existing build to Conan,
or upgrading a compiler, configure once with `cmake --fresh --preset clang-ninja`.

Dependency requirements are in [conanfile.py](../conanfile.py), recipe revisions
in [conan.lock](../conan.lock), and the Conan version in [uv.lock](../uv.lock).

## Optional Python backends

On Ubuntu, install the matching Python development files:

```sh
sudo apt-get install -y python3-dev
```

Homebrew's Python includes its development files on macOS. Then follow
[backend installation](backends.md#install-an-optional-backend).
Python package constraints are in [pyproject.toml](../pyproject.toml);
[uv.lock](../uv.lock) records reproducible resolutions.

## Offline builds

Complete dependency preparation while online. CMake configure, build, and test
commands do not contact Conan remotes. To regenerate `.conan/` from packages
already cached locally, rerun the Conan install command with `--no-remote`.

Continue with [your first simulation](quickstart.md).
