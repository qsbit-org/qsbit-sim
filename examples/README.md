# Executable examples

CMake assembles these sources into `build/examples/*.elf` using GNU RISC-V binutils.
`quantum.inc` uses `.insn`; the simulator never parses assembly. `link.ld` provides a
bare-metal entry at zero and writable data at 0x1000. All instructions are RV32I or
custom-0; compressed instructions and relaxation are disabled.

## Bell pair

`bell.S` applies H to qubit 0, CX to qubits 0 and 1, then measures both at one timing
point. Two QAPPEND instructions complete the measurement group before it is sealed.
QREAD returns the live results and stores them at addresses 4096 and 4100.

```sh
build/qsbit-sim --program build/examples/bell.elf --backend aer \
  --trace out/bell.jsonl --summary out/bell.json --inspect 4096 --inspect 4100
```

The ideal result is 00 or 11 with equal probability; both bits must agree. Qubit 0 is
the least significant statevector bit. A seed is reproducible for this pinned adapter;
it does not define a common random stream with another simulator library.

## Measurement feedback

`feedback.S` applies X to qubit 0, measures it, waits through QREAD, and takes an RV32I
branch. Result one schedules X on qubit 1; result zero schedules Z. Aer deterministically
returns one for this preparation, yielding `|11>`. Scripted runs cover both branches:

```sh
build/qsbit-sim --program build/examples/feedback.elf --backend scripted --outcomes 0 \
  --trace out/feedback-zero.jsonl --summary out/feedback-zero.json --inspect 4096
```

The branch computes future control while the TCU timer continues. Its long explicit
interval provides CPU and crossing slack; result waiting does not pause or rebase time.

## Constant pulse

`pulse.S` selects the default X-drive descriptor: duration 20 ns and amplitude pi/20
radians/ns. The pulse backend evolves with `H = amplitude * X / 2`, then measures.
The result is one, stored at 4096. Selecting the Aer circuit backend for this program
fails with `UnsupportedCapability` before pulse execution.

```sh
build/qsbit-sim --program build/examples/pulse.elf --backend pulse \
  --trace out/pulse.jsonl --summary out/pulse.json --inspect 4096
```

## Default-profile event times

| Example | Physical starts in nanoseconds |
| --- | --- |
| Bell | H: 1160; CX: 1200; both acquisitions: 1240. |
| Feedback | X on q0: 1160; acquisition: 1240; selected gate on q1: 3560. |
| Pulse | X drive: 1160; acquisition: 1240. |

Default acquisition duration is 40 ns and discriminator delay is 20 ns. Results for
the acquisitions at 1240 are ready at 1300, CPU-visible at 1305, and fast-visible at
1340. QEND retirement is not simulator completion: outstanding actions and both
feedback paths must drain. The summary contains the actual stop tick and memory bits.

Tests assert exact event times, final state or signatures, retirement identities and
successful drain. A replacement timing profile changes these numerical expectations.

## Overlapping drives

`overlap.S` with `overlap.json` starts X and Z drives together on qubit 0 through
separate ports and a shared nonexclusive resource. The pulse backend integrates their
sum for 20 ns. The expected state is `-i (|0> + |1>) / sqrt(2)` on qubit 0; qubit 1
remains zero. Sequentially replaying the two rotations would give a different result.

```sh
build/qsbit-sim --program build/examples/overlap.elf --backend pulse \
  --profile examples/overlap.json --trace out/overlap.jsonl --summary out/overlap.json
```
