# Run your first simulation

Build the simulator and run a program that measures qubit 0, reads the result,
and selects a gate on qubit 1.

First install the [prerequisites](prerequisites.md) and prepare the Conan
dependencies. This example uses the mock backend, so no Python quantum
packages are needed.

## Build the simulator and examples

Run these commands from the repository root:

```sh
cmake --preset clang-ninja
cmake --build --preset clang-ninja --parallel
```

CMake builds the simulator as `build-clang/qsbit-sim`. It also assembles the
programs in [examples](../examples/README.md) and places their ELF files and
run configurations under `build-clang/examples/`.

For GCC on Ubuntu, first [prepare its Conan dependencies](prerequisites.md#prepare-conan-dependencies).
Then use the `gcc-ninja` preset and `build-gcc` directory throughout.
See [build options](building.md) to select another build configuration or
enable tests.

## Run the feedback program

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/mock.json
```

The run configuration selects the feedback ELF, the mock backend and a
measurement outcome of 1. All paths in this JSON file are relative to the file.
The run creates:

| File | Contents |
| --- | --- |
| `build-clang/runs/mock.json` | Success status, final registers, inspected memory and the simulation profile. |
| `build-clang/runs/mock.jsonl` | Timestamped instruction, control, device and feedback events. |

Open the summary. `success` should be `true` and `memory["4096"]` should be `1`.
The program stores its measured bit at address 4096.

## Follow the result

[Acquisition](glossary.md#acquisition) is the timed readout interval. At its end,
the backend supplies a bit; the modeled [discriminator](glossary.md#discrimination)
delay determines when that bit is ready to send.

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
| 500 | The modeled discriminator delay ends; the bit is ready for delivery. |
| 505 | The CPU receives the result. |
| 720 | The branch-selected X starts on qubit 1. |

After receiving the result, the CPU branches and prepares the selected event
for cycle 26. The TCU enqueues it at 680 ns, then triggers it at
`200 + 26 * 20 = 720 ns`. The TCU timer continues while QREAD waits.

## Try the other branch

Override the mock outcome and use separate output files:

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/mock.json \
  --outcomes 0 \
  --summary build-clang/runs/feedback-zero.json \
  --trace build-clang/runs/feedback-zero.jsonl
```

The new summary should report `memory["4096"]` as `0`. The operation at
720 ns is now Z on qubit 1. The mock backend returns the requested bit
regardless of the preceding X; it does not evolve quantum state.

Open the [execution player](execution.md) to step through both branches.
To obtain measurements from a simulated quantum state, follow the
[Aer setup](backends.md#install-an-optional-backend).
