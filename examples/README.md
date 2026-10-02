# Executable examples

With `QSBIT_BUILD_EXAMPLES=ON` or `BUILD_TESTING=ON`, CMake assembles these sources
into `<build-dir>/examples/*.elf` using GNU RISC-V binutils and copies the run
configurations into `<build-dir>/examples/runs/`.
`quantum.inc` uses `.insn`; the simulator never parses assembly. `link.ld` provides a
bare-metal entry at zero and writable data at 0x1000. All instructions are RV32I or
custom-0; compressed instructions and relaxation are disabled.

## Control-only run

Run the feedback example with the mock backend, without the Python bridge:

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/mock.json
```

Results are in `build-clang/runs/mock.json` and `build-clang/runs/mock.jsonl`.
For the remaining examples, enable the Python bridge and install the selected
[backend](../docs/backends.md). Run commands from the repository root;
the commands below use the `clang-ninja` build.

## Bell pair

For a repeated single-qubit experiment with noise and paper-based timing checks,
see [QuMA AllXY](allxy/README.md).

`bell.S` applies H to qubit 0, CX to qubits 0 and 1, then measures both at one time
point. Two cw instructions prepare the acquisition events; FMR enqueues
them and returns each result. The program stores the bits at addresses 4096 and 4100.

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/bell.json
```

The ideal result is 00 or 11 with equal probability; both bits must agree. Qubit 0 is
the least significant statevector bit. Reusing the seed with the same adapter
and dependencies reproduces the outcomes.

## Measurement feedback

`feedback.S` applies X to qubit 0, measures it, waits through FMR, and takes an RV32I
branch. Result one schedules X on qubit 1; result zero schedules Z. Aer deterministically
returns one for this preparation, yielding `|11>`. Mock runs cover both branches:

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/feedback.json --backend mock --outcomes 0
```

The CPU receives the result at 505 ns. wait moves the current time point
from cycle 12 to cycle 26. Both branches enqueue the selected event at 700 ns
for output at 720 ns. The TCU timer continues while the CPU waits and branches.

## Constant pulse

`pulse.S` selects the default X-drive descriptor: duration 20 ns and amplitude pi/20
radians/ns. The pulse backend evolves with `H = amplitude * X / 2`, then measures.
The result is one, stored at 4096. Selecting the Aer circuit backend for this program
fails with `UnsupportedCapability` before pulse execution.

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/pulse.json
```

## Example event times

The run configurations set TCU start to 200 ns. Other timing settings use their
defaults. Running an ELF directly without this configuration uses the simulator
default start of 1000 ns; pass `--start 200` to reproduce these example times.

| Example | Physical starts in nanoseconds |
| --- | --- |
| Bell | H: 360; CX: 400; both acquisitions: 440. |
| Feedback | X on q0: 360; acquisition: 440; selected gate on q1: 720. |
| Pulse | X drive: 360; acquisition: 440. |

Default acquisition duration is 40 ns and discriminator delay is 20 ns. Results for
the acquisitions at 440 are ready at 500, CPU-visible at 505, and TCU-visible at
540. Simulation completion waits for outstanding events and both feedback paths.
The summary contains the stop tick and memory bits.

## Overlapping drives

`overlap.S` with `overlap.json` starts X and Z drives together on qubit 0 through
separate ports and a shared nonexclusive resource. The pulse backend integrates their
sum for 20 ns. The expected state is `-i (|0> + |1>) / sqrt(2)` on qubit 0; qubit 1
remains zero. Sequentially replaying the two rotations would give a different result.

```sh
build-clang/qsbit-sim --config build-clang/examples/runs/overlap.json
```
