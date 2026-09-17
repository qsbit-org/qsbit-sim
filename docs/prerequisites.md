# Install build prerequisites

Run the build commands from the repository root. Install only the tools needed
for the workflow you plan to use:

| Workflow | Required tools |
| --- | --- |
| Build the default simulator | CMake 3.24 or newer, Ninja, and a C++20-capable Clang or GCC toolchain (C and C++ compilers). |
| Build and run the bundled examples | Default build tools plus GNU RISC-V binutils. |
| Run the core test suite | Example tools plus a Python interpreter. Python quantum packages are not required. |
| Build the optional Python bridge | Default build tools plus a Python interpreter, matching Python development files, and the project package installed in `.venv`. |

## Ubuntu

The CI build uses Ubuntu 24.04. Install the tools for the default Clang build:

```sh
sudo apt-get update
sudo apt-get install -y clang cmake ninja-build
```

For the GCC build, install `gcc` and `g++` instead of `clang`:

```sh
sudo apt-get install -y gcc g++ cmake ninja-build
```

To build the bundled examples, also install GNU RISC-V binutils:

```sh
sudo apt-get install -y binutils-riscv64-unknown-elf
```

The examples use `riscv64-unknown-elf-as` and `riscv64-unknown-elf-ld` to
produce RV32I ELF files. The core tests also use
`riscv64-unknown-elf-objdump`. A RISC-V C compiler is not required for these
workflows. Install Python for the core tests:

```sh
sudo apt-get install -y python3
```

If you enable the Python bridge, install the venv support and development files
for the Python interpreter you will use:

```sh
sudo apt-get install -y python3-venv python3-dev
```

Then create `.venv` and install the selected optional backend as shown in the
[README](../README.md#optional-quantum-backends). Python package requirements
are declared in [`pyproject.toml`](../pyproject.toml); reproducible uv
resolutions are in [`uv.lock`](../uv.lock).

## Check the installed tools

Run the checks for your chosen workflow:

```sh
cmake --version
ninja --version
clang --version
clang++ --version
```

For GCC, check `gcc --version` and `g++ --version` instead. Confirm that CMake
reports version 3.24 or newer. For examples and tests, check the RISC-V tools:

```sh
riscv64-unknown-elf-as --version
riscv64-unknown-elf-ld --version
riscv64-unknown-elf-objdump --version
```

For tests or the Python bridge, check `python3 --version`. When using a
different Python installation for the bridge, its development files must match
that interpreter; activate its `.venv` before configuring CMake.

## SystemC and offline builds

CMake first looks for an installed C++20 SystemC library. If it cannot find
one, the default configuration downloads and builds the revision pinned in
[`cmake/SystemC.cmake`](../cmake/SystemC.cmake) during configure. Provision that
source or an installed SystemC library before an offline configure; see the
[SystemC build options](building.md#systemc). Build options and test commands
are in [building.md](building.md).
