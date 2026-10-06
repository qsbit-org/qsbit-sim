# Test the simulator

The test suite checks instruction results, cycle timing, control protocols and
device behavior. Tests that cross a module boundary assert timestamps and final
state as well as successful completion.

Run commands from the repository root. The
[build guide](building.md#enable-tests) shows how to enable tests.

## Run the fast suite

```sh
cmake --preset gcc-ninja -DBUILD_TESTING=ON
cmake --build --preset gcc-ninja --parallel
ctest --test-dir build-gcc -L fast --output-on-failure
```

List registered tests with `ctest --test-dir build-gcc -N`.
Select a test by name with `-R`, for example:

```sh
ctest --test-dir build-gcc -R '^control\.' --output-on-failure
```

Optional dependencies and build flags determine which tests are registered.

## Before opening a pull request

Install the development tools and activate the local environment:

```sh
uv sync --frozen --extra dev
source .venv/bin/activate
pre-commit install --install-hooks
pre-commit run --all-files
```

The installation enables both `pre-commit` and `commit-msg` hooks. They check
commit messages, C++ formatting, Python lint and formatting with Ruff,
whitespace, merge markers, and YAML, JSON and
TOML syntax. If a hook changes a file, review and stage the edit, then rerun
the checks. With pip, install `.[dev]` into `.venv` instead of using uv.

Build and run the fast suite before opening a pull request:

```sh
cmake --build --preset gcc-ninja --parallel
ctest --test-dir build-gcc -L fast --output-on-failure
```

Run the relevant optional or integration tests for the changed behavior.
CI checks every commit introduced by a push or pull request against the
[commit-message requirement](../AGENTS.md#repository-hygiene), runs the pre-commit
hooks on all tracked files and runs the complete configured CTest suites.

## What each test family checks

| Test family | Main assertions |
| --- | --- |
| `core.*` | RV32I effects, legal encodings, image access, mailboxes and memory service. |
| `config.profile` | Profile JSON round trips, typed event fields and invalid configurations. |
| `cpu.vliw` | Dual-codeword operand modes, reserved encodings, blocked-operation progress and reset. |
| `cpu.trace` | End-of-edge pipeline snapshots, stalls, branch flushes, reset and core identity. |
| `backend.execution` | Batch limits, operation order, measurement boundaries, inspection, reset and failures. |
| `device.two_qubit` | Paired gate inputs, single application, delay compensation, conflicts and reset. |
| `sync.neighbor` | Neighbor countdowns, directional delays, early signals, capacity and reset. |
| `decoder.*` | Bounded transport, response timing, correction accumulation and reset under backpressure; optional PyMatching configuration and correction masks. |
| `control.*` | Atomic enqueue, event-ID lists, queue bounds, deadlines, measurement registers and execution flags. |
| `protocol.*` | Held operations, capacity faults, resources, readout timing, reset, overflow and execution flag updates. |
| `systemc.*` | ELF execution, pipeline and feedback timing, reset, CLI behavior and process-registration order. |
| `adapter.*` | Replacement CPU construction through `ICpuCycleModel` and session reset. |
| `docs.*` | Documentation links, checked declarations and registered test references. |
| `build.dependencies` | Missing-setup diagnostics, isolated Conan discovery, default example assembly, and simulator-only configuration without RISC-V tool discovery. |

Each [module page](modules/README.md) names its relevant CTest entries and
describes what those tests establish.

## Timing regressions

When changing a protocol, check the boundary case that distinguishes the old
and new behavior. The existing suite includes:

- Two `cw` instructions separated by `wait.i 0` at one time point, followed by one complete enqueue.
- Requests published exactly on a receiver edge, and enqueue while a full
  queue releases events. Neither the new request nor the newly freed space
  can be used on that edge.
- Empty timing queues during CPU result waits. The timer continues and
  late time points fail without shifting their deadlines.
- Taken branches and older faults that discard younger control instructions.
- Overlapping resource intervals, adjacent intervals and same-target sampling
  collisions. Invalid batches must not partially change device state.
- Delayed discriminator arms, zero discriminator delay, independent CPU and
  fast-result mailbox communication, and execution flag updates.
- Reset at clock and physical boundaries, stale completions and measurement register validity.
- Exit ECALL while physical work or fast-feedback acknowledgments remain pending.

Registration-order tests run equivalent scenarios with reversed SystemC process
registration and compare the resulting trace and state.

## SystemC test processes

Run each independent SystemC scenario in a fresh executable or child process.
[Elaboration](glossary.md#elaboration) constructs the SystemC modules and processes.
A normal C++ fixture cannot assume it can repeat that setup or rewind simulation time.
Keep pure ISA and transition tests independent of SystemC where possible.

Use deterministic inputs and seeds. Assert event times, required ordering,
final registers or memory, and the stop reason. When a comparison fails, report
the seed and first differing event so the run can be reproduced.

## Optional backend tests

Enable `QSBIT_PYTHON_BACKENDS` and the selected numerical test options described
in [building](building.md#cmake-options).
`python.plugin` checks adapter loading without numerical packages.
`python.backend_configuration` checks discovery, configuration validation and
`--check-config`. `QSBIT_TEST_AER` adds thermal relaxation and density-matrix checks;
`QSBIT_TEST_STIM` adds Clifford gates, depolarization and full-program execution.
`numerical.*` checks Aer Bell correlations, feedback and unsupported operations.
`QSBIT_TEST_QUTIP` registers `python.qutip`, covering analytic joint evolution,
waveform continuity, dissipation, collapse and native acquisition metadata.
`QSBIT_TEST_QEC` registers `decoder.pymatching` for PyMatching configuration
validation and correction-mask checks; install the `qec` extra to run it.

## Independent ISA checks

`reference.rv32_random` compares every retired register state and PC, plus final
memory, against Unicorn using deterministic generated programs.

`reference.rv32_architecture` runs the applicable pinned RV32I architecture-test
bodies and compares retirements and memory signatures. Its adapter replaces
platform entry and exit and uses RV32I NOP padding in the upstream address-load
helper; it does not modify the generated instruction-test bodies.

One case requiring privileged trap handling is explicitly excluded. Native
tests check the corresponding typed alignment fault. These checks do not
constitute official RISC-V certification.

The runners and dependency manifests pin their reference versions. Test programs,
traces and reports remain in the build tree.

## Cross-project integration

The [integration workflow](../.github/workflows/integration.yml) builds this
revision with `qsbit-compiler` main. The compiler repository also tests its
own changes against qsbit-sim main. Both workflows run on pushes and pull
requests and record the two Git revisions.

The tests compile QIR to ELF and execute it in the simulator. The mock test
checks operation timing and measurement-result order. The Aer test checks
Bell-state amplitudes and measurement correlations. The QEC test uses Stim
and PyMatching to check adaptive loops, measurement-result reuse, conditional
corrections and late decoder feedback.

## CI, sanitizers and coverage

The [CI workflow](../.github/workflows/ci.yml) runs a core sanitizer build, a
bridge-only build, Aer tests, Stim tests, QuTiP tests and documentation checks. It checks
formatting and optional-dependency isolation and preserves diagnostic artifacts.
Compiler warnings are errors.

Use the [sanitizer build](building.md#sanitizers) to check address and
undefined-behavior errors. For line and branch coverage, use a separate build:

```sh
cmake --preset gcc-ninja -B build-coverage \
  -DBUILD_TESTING=ON -DQSBIT_COVERAGE=ON
cmake --build build-coverage --parallel
ctest --test-dir build-coverage --output-on-failure
python3 tools/coverage.py --build build-coverage
```

Coverage artifacts stay under the build directory. Review uncovered error
conditions and timing cases.

## Documentation checks

```sh
ctest --test-dir build-gcc -L documentation --output-on-failure
```

`docs.contracts` checks local links and heading targets, compares marked C++
excerpts with headers, and checks module CTest references. `docs.checker` tests
that validator's failure cases. After changing a header, refresh excerpts with:

```sh
python3 tools/check_docs.py --build build-gcc --write
```

Run the relevant simulator tests for behavioral changes and the
[website tests](website.md#run-website-tests) for rendered pages and diagrams.
