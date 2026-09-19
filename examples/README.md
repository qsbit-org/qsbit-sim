# Executable examples

With `QSBIT_BUILD_EXAMPLES=ON` or `BUILD_TESTING=ON`, CMake assembles these sources
into `<build-dir>/examples/*.elf` using GNU RISC-V binutils and copies the run
configurations into `<build-dir>/examples/runs/`.
`quantum.inc` uses `.insn`; the simulator never parses assembly. `link.ld` provides a
bare-metal entry at zero and writable data at 0x1000. All instructions are RV32I or
custom-0; compressed instructions and relaxation are disabled.

## Control-only run

The default C++ build can execute a scripted feedback scenario without Python:

```sh
build/qsbit-sim --config build/examples/runs/scripted.json
```

Results are in `build/runs/scripted.json` and `build/runs/scripted.jsonl`.
For the remaining examples, enable the Python bridge and install the selected
[backend extra](../docs/backends.md). Run each configuration with the simulator
from the same build directory. For example, a `clang-ninja` preset build uses
`build-clang/qsbit-sim --config build-clang/examples/runs/scripted.json`.

## Bell pair

`bell.S` applies H to qubit 0, CX to qubits 0 and 1, then measures both at one timing
point. Two QAPPEND instructions complete the measurement group before it is sealed.
QREAD returns the live results and stores them at addresses 4096 and 4100.

```sh
build/qsbit-sim --config build/examples/runs/bell.json
```

The ideal result is 00 or 11 with equal probability; both bits must agree. Qubit 0 is
the least significant statevector bit. A seed is reproducible for this pinned adapter;
it does not define a common random stream with another simulator library.

## Measurement feedback

`feedback.S` applies X to qubit 0, measures it, waits through QREAD, and takes an RV32I
branch. Result one schedules X on qubit 1; result zero schedules Z. Aer deterministically
returns one for this preparation, yielding `|11>`. Scripted runs cover both branches:

```sh
build/qsbit-sim --config build/examples/runs/feedback.json --backend scripted --outcomes 0
```

The CPU receives the result at 505 ns. Both branches submit a group that the TCU
admits at 680 ns. QADVANCE moves the cursor from cycle 12 to cycle 26, so the
selected gate starts at 720 ns, two TCU cycles after admission. The TCU timer
continues throughout the CPU work.

## Constant pulse

`pulse.S` selects the default X-drive descriptor: duration 20 ns and amplitude pi/20
radians/ns. The pulse backend evolves with `H = amplitude * X / 2`, then measures.
The result is one, stored at 4096. Selecting the Aer circuit backend for this program
fails with `UnsupportedCapability` before pulse execution.

```sh
build/qsbit-sim --config build/examples/runs/pulse.json
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
the acquisitions at 440 are ready at 500, CPU-visible at 505, and fast-visible at
540. QEND retirement is not simulator completion: outstanding actions and both
feedback paths must drain. The summary contains the actual stop tick and memory bits.

Tests assert exact event times, final state or signatures, retirement identities and
successful drain. A replacement simulation profile changes these numerical expectations.

## Overlapping drives

`overlap.S` with `overlap.json` starts X and Z drives together on qubit 0 through
separate ports and a shared nonexclusive resource. The pulse backend integrates their
sum for 20 ns. The expected state is `-i (|0> + |1>) / sqrt(2)` on qubit 0; qubit 1
remains zero. Sequentially replaying the two rotations would give a different result.

```sh
build/qsbit-sim --config build/examples/runs/overlap.json
```
