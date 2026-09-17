# Install build prerequisites

Choose the tools for the workflow you need. A normal simulator build requires
CMake and a C++20 toolchain. Examples additionally require GNU RISC-V binutils;
tests require a Python interpreter.

| Workflow | Required tools |
| --- | --- |
| Build the simulator | CMake 3.24 or newer and a C++20 Clang or GCC toolchain. The checked-in presets use Ninja. |
| Build examples | Build tools plus GNU RISC-V assembler and linker. |
| Run core tests | Example tools, RISC-V objdump and Python. |
| Use Python backends | Build tools, Python, matching development files, and a local `.venv` with the selected adapter. |
| Build the website | Example tools, Python documentation dependencies, Doxygen and Graphviz. See [website setup](website.md). |

CMake finds an installed C++20 SystemC library or downloads the pinned source
during configuration. A default C++ build does not require Python quantum packages.

## Ubuntu

CI uses Ubuntu 24.04. For GCC and the bundled examples, install:

```sh
sudo apt-get update
sudo apt-get install -y gcc g++ cmake ninja-build binutils-riscv64-unknown-elf
```

For Clang, install `clang` and select the `clang-ninja` preset. The toolchain
also needs the C and assembly tools used by the SystemC dependency.

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

## Offline builds

Before configuring offline, provide an installed SystemC library or a local
checkout of the revision pinned in [cmake/SystemC.cmake](../cmake/SystemC.cmake).
[SystemC build options](building.md#systemc) show how to select either.
Rebuilding an already configured tree does not fetch dependencies.

Continue with [your first simulation](quickstart.md).
