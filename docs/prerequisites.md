# Install build prerequisites

Choose the tools for the workflow you need. A normal simulator build requires
CMake, a C++20 toolchain, and an installed C++20 SystemC 3.0.1. Examples
additionally require GNU RISC-V binutils; tests require a Python interpreter.

| Workflow | Required tools |
| --- | --- |
| Build the simulator | CMake 3.24 or newer, a C++20 Clang or GCC toolchain, Git to install SystemC, and an installed C++20 SystemC 3.0.1. The checked-in presets use Ninja. |
| Build examples | Build tools plus GNU RISC-V assembler and linker. |
| Run core tests | Example tools, RISC-V objdump and Python. |
| Use Python backends | Build tools, Python, matching development files, and a local `.venv` with the selected adapter. |
| Build the website | Example tools, Python documentation dependencies, Doxygen and Graphviz. See [website setup](website.md). |

A default C++ build does not require Python quantum packages.

## Ubuntu

CI uses Ubuntu 24.04. For GCC and the bundled examples, install:

```sh
sudo apt-get update
sudo apt-get install -y gcc g++ git cmake ninja-build binutils-riscv64-unknown-elf
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

## SystemC

Install the pinned SystemC 3.0.1 revision before configuring qsbit-sim. The
following commands work from any directory and install it under
`$HOME/.local/systemc` using Clang and C++20:

```sh
mkdir -p "$HOME/.local/src"
git clone https://github.com/accellera-official/systemc.git "$HOME/.local/src/systemc"
git -C "$HOME/.local/src/systemc" checkout 11ad094d282fd5330b27ab57f90f9d231a763da1
cmake -S "$HOME/.local/src/systemc" -B "$HOME/.local/src/systemc-build" -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_ASM_COMPILER=clang \
  -DCMAKE_CXX_STANDARD=20 -DCMAKE_INSTALL_PREFIX="$HOME/.local/systemc" \
  -DENABLE_EXAMPLES=OFF -DENABLE_REGRESSION=OFF
cmake --build "$HOME/.local/src/systemc-build" --parallel
cmake --install "$HOME/.local/src/systemc-build"
```

For a GCC build, replace `clang`, `clang++`, and `clang` with `gcc`, `g++`,
and `gcc` in the configure command. If switching toolchains, use a fresh
SystemC build directory. To install system-wide, set
`-DCMAKE_INSTALL_PREFIX=/usr/local` and run `sudo cmake --install` for that
build directory. Reconfigure qsbit-sim with
`-DCMAKE_PREFIX_PATH="$HOME/.local/systemc"` for the user-local installation;
standard system prefixes are searched automatically. A SystemC installation
built with another C++ standard is rejected at configure time. The simulator
does not download SystemC.

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
The installed SystemC package can be checked by configuring the simulator with
the appropriate `CMAKE_PREFIX_PATH`.

## Offline builds

Install SystemC and the required tools before working offline. The simulator
does not fetch dependencies at configure or build time. Use
`-DCMAKE_PREFIX_PATH` to select the installed SystemC package.

Continue with [your first simulation](quickstart.md).
