# CACTUS timing validation

The `reference.cactus_golden` CTest case runs RV32I extension programs with
qsbit-sim and compares their observable events with traces captured from
[CACTUS](https://github.com/gtaifu/CACTUS). CI runs the test on every push and
pull request. The test uses checked in reference traces, so CI does not build
CACTUS. The raw JSONL and normalized golden events are under
[`tests/data/cactus`](tests/data/cactus).

## Reference and programs

The CACTUS source revision is
[`a05f47423ac37b14989ec38c525741ec597b4826`](https://github.com/gtaifu/CACTUS/commit/a05f47423ac37b14989ec38c525741ec597b4826).
The eQASM assembler revision is
[`9b024494558287a937cdd5a8ccd63fcaa784b1c4`](https://github.com/QE-Lab/eQASM_Assembler/commit/9b024494558287a937cdd5a8ccd63fcaa784b1c4).
The reference harness uses
SystemC 2.3.3 with 5 ns and 20 ns clocks. It resets the model before publishing
the run signal at 999 ns and observes events through 12000 ns. The harness
source, hardware configuration, gate configuration, opcode map, layout, and
assembler compatibility patch are in
[`tests/data/cactus/reference`](tests/data/cactus/reference).

Each workload has an eQASM program and an independently assembled RV32I
extension program in `tests/data/cactus/workloads`. The two profiles are in
`tests/data/cactus/profiles`; `pressure` limits timing and event capacity to
two. The programs encode the same ordered operation groups. qsbit-sim executes
its ELF without reading the CACTUS schedule. All measurements have
deterministic basis state results. CACTUS uses QuantumSim, while qsbit-sim uses
Aer. The mapping covers seven qubits. Each mapped command has a 40 ns delay;
single qubit gates and measurements have a 20 ns duration. Both native
profiles start at 1040 ns and set CPU result latency to 12 ns.

| Workload | Operation sequence and purpose |
| --- | --- |
| `x_measure` | X on qubit 0, then measurement; checks result one. |
| `zero_measure` | Z on qubit 0, then measurement; checks result zero. |
| `parallel` | X on qubits 0 and 1, then simultaneous measurements. |
| `repeat` | X, measure, X, measure on qubit 0; checks repeated results. |
| `entangle_uncompute` | H on 0 and 3, two CZ operations, H on both, then measurements; checks a two qubit path returning to a basis state. |
| `pressure` | Forty alternating X groups on qubits 0 and 1, then two measurements; the native timing and event capacities are two. |
| `feedback_0` | Z and measurement on qubit 0, then a branch selects Z on qubit 1. |
| `feedback_1` | X and measurement on qubit 0, then a branch selects X on qubit 1. |

The first six workloads run in both unmodified and corrected CACTUS binary
modes. The corrected mode changes two measurement identification expressions.
Each feedback workload runs in unmodified assembly mode and corrected binary
mode. These combinations make 16 cases. The eQASM programs include eight
terminal `qwait` guards because CACTUS prefetches timing groups. The guards
contain no quantum operations. The reference must report no queue error in the
observation window. Native `QEND` drains all outstanding work.

## What the comparison asserts

The test compares TCU label output; device operation, targets, and start tick;
and measurement result readiness. It also compares CPU result visibility where
CACTUS exposes that event. Independent events at the same tick are sorted before
comparison. Timestamps use one common nanosecond origin, with zero tolerance
and no event by event realignment. The runner reports the first divergent event
or an event count mismatch. Before running qsbit-sim, it normalizes each raw
CACTUS trace and checks it against the stored golden JSON.

For example, the raw `x_measure-original` trace begins with the run signal and
then records the first label and operation:

```jsonl
{"kind":"RunPublished","tick":999}
{"kind":"TcuOutput","tick":1200,"label":1,"interval":8}
{"kind":"DeviceCommand","tick":1240,"cycle":12,"operation":"x180","targets":[0]}
```

The same trace later records measurement readiness at 1340 ns with value one.
The normalized golden keeps the comparable TCU, operation, and result events;
it omits the run signal and fields such as `cycle` that only diagnose CACTUS.

The unmodified binary measurement path has two source defects: its measurement
bit masks are tested incorrectly, and its opcode comparison constructs an
integer wrapper with the wrong value. The corrected variant changes only those
two expressions. Its patch is stored in the reference fixture directory. CPU
visibility is omitted for the six unmodified binary cases. In `repeat`, CACTUS
publishes only the latest result after all pending measurements on that target
finish. Both modes therefore omit per measurement CPU visibility for this
workload.

These comparisons do not establish operation end timing, private CPU pipeline
timing, or equivalence of the two termination protocols. CACTUS does not
implement the fast conditional execution path. Native tests cover `QAPPEND_IF`
separately.

## Run the regression

From the repository root, provision the pinned build dependencies as described
in [Prerequisites](docs/prerequisites.md) and install the optional Aer extra
from [the dependency manifest](pyproject.toml). Then run:

```sh
cmake --preset clang-ninja -DBUILD_TESTING=ON -DQSBIT_PYTHON_BACKENDS=ON -DQSBIT_TEST_AER=ON
cmake --build --preset clang-ninja
ctest --test-dir build-clang -R '^reference\.cactus_golden$' --output-on-failure
```

The test assembles the checked in RV32I source with the build's RISC-V tools.
It writes ELF files, native traces, summaries, and logs under
`build-clang/cactus-golden`. It does not rewrite the fixtures. `manifest.json`
names each case, its probe set, event count, and trace and binary hashes. `traces/`
contains raw CACTUS JSONL; `golden/` contains normalized events. The test
covers 16 cases and 308 compared events.

To refresh a fixture, rebuild the pinned CACTUS revision and eQASM assembler in
a separate workspace. Apply `assembler-compatibility.patch` to the assembler;
it enables the stored qubit layout and removes its unused Python binding target.
Compile the stored eQASM with `opcodes.qmap` and `layout.txt`. Build
`full_probe.cpp` against the pinned CACTUS libraries and SystemC 2.3.3. Run it
first against the unmodified reference, then build a separately named variant
with `binary-measurement-fixes.patch` for corrected cases. Use
`assembly-hardware.json` for assembly mode and `hardware.json` for binary mode;
both modes use `gates.json` and `log-levels.json`. Review the raw trace,
normalized diff, binaries, and revision hashes before updating a golden file.
The original 16 program runs passed 308 compared events with zero tick error.
