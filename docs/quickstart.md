# Run your first simulation

This tutorial runs a program that measures one qubit and uses the result to
choose an operation on another qubit. You will build the simulator, execute the
program and check its output.

You need the [build tools and GNU RISC-V binutils](prerequisites.md).
The example uses the built-in scripted backend, which supplies a fixed
measurement bit. It exercises the complete control path without installing a
numerical quantum simulator.

## Build the simulator and examples

Run these commands from the repository root:

```sh
cmake --preset gcc-ninja -DQSBIT_BUILD_EXAMPLES=ON
cmake --build --preset gcc-ninja --parallel
```

CMake builds the simulator as `build-gcc/qsbit-sim`. It also assembles the
programs in [examples](../examples/README.md) and places their ELF files and
run configurations under `build-gcc/examples/`.

For Clang, use the `clang-ninja` preset and `build-clang` directory throughout.
See [build options](building.md) to use an existing SystemC installation or
enable tests.

## Run the feedback program

```sh
build-gcc/qsbit-sim --config build-gcc/examples/runs/scripted.json
```

The run configuration selects the feedback ELF, the scripted backend and a
measurement outcome of 1. All paths in this JSON file are relative to the file.
The run creates:

| File | Contents |
| --- | --- |
| `build-gcc/runs/scripted.json` | Success status, final registers, inspected memory and the timing profile. |
| `build-gcc/runs/scripted.jsonl` | Timestamped instruction, control, device and feedback events. |

Open the summary. `success` should be `true` and `memory["4096"]` should be `1`.
The program stores its measured bit at address 4096.

## Follow the result

The program first applies X to qubit 0, then measures it. QREAD waits for the
measurement to reach the CPU. An RV32I branch then selects X on qubit 1 for
result 1, or Z for result 0.

The run file sets TCU start to 200 ns. With a 20 ns TCU period, the first
operation at cycle 8 starts at `200 + 8 * 20 = 360 ns`. The trace contains:

| Time (ns) | Event |
| --- | --- |
| 360 | X starts on qubit 0. |
| 440 | Acquisition starts on qubit 0. |
| 480 | The backend supplies the measurement bit. |
| 500 | Discrimination finishes; the result is ready for delivery. |
| 505 | The CPU receives the result. |
| 720 | The branch-selected X starts on qubit 1. |

After receiving the result at 505 ns, the CPU executes the branch and submits
the selected action. Both branches reach TCU admission at 680 ns. The program
advances its cursor from cycle 12 to cycle 26, scheduling output at
`200 + 26 * 20 = 720 ns`. This leaves two TCU cycles between admission and
output. Waiting for a result does not pause the TCU.

## Try the other branch

Override the scripted outcome and use separate output files:

```sh
build-gcc/qsbit-sim --config build-gcc/examples/runs/scripted.json \
  --outcomes 0 \
  --summary build-gcc/runs/feedback-zero.json \
  --trace build-gcc/runs/feedback-zero.jsonl
```

The new summary should report `memory["4096"]` as `0`. The operation at
720 ns is now Z on qubit 1. The scripted backend returns the requested bit
regardless of the preceding X; it does not evolve quantum state.

Open the [execution player](execution.md) to step through both branches.
To obtain measurements from a simulated quantum state, follow the
[Aer setup](backends.md#install-an-optional-backend).
