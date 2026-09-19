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

Install SystemC with C++20 before configuring qsbit-sim. Any installation
prefix is supported. CMake discovers standard system installations automatically;
custom prefixes need to be added to its search path once in your shell.

To build the pinned revision, run the following from a source workspace outside
the qsbit-sim checkout. These commands use Clang and CMake's default Unix install
prefix, `/usr/local`:

```sh
git clone https://github.com/accellera-official/systemc.git
git -C systemc checkout 11ad094d282fd5330b27ab57f90f9d231a763da1
cmake -S systemc -B systemc/build -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_ASM_COMPILER=clang \
  -DCMAKE_CXX_STANDARD=20 -DENABLE_EXAMPLES=OFF -DENABLE_REGRESSION=OFF
cmake --build systemc/build --parallel
sudo cmake --install systemc/build
```

For GCC, replace the three compiler settings with `gcc`, `g++`, and `gcc`.
Use a fresh SystemC build directory when changing compilers.

For an installation without administrator access, choose any writable prefix.
Instead of the `sudo` install step above, run:

```sh
cmake -S systemc -B systemc/build -DCMAKE_INSTALL_PREFIX=/path/to/systemc
cmake --install systemc/build
export CMAKE_PREFIX_PATH=/path/to/systemc
```

Replace `/path/to/systemc` with your chosen installation directory. Set this
variable in each development shell, or add it to your shell startup file.
If it already contains other dependency prefixes, add the SystemC prefix to
that list, separated by `:` on Linux. The normal qsbit-sim build commands stay
the same for every installation location.

A SystemC installation built with another C++ standard is rejected at configure
time. The simulator does not download SystemC.

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
does not fetch dependencies at configure or build time. Keep any custom
dependency prefixes in your shell environment as described above.

Continue with [your first simulation](quickstart.md).
