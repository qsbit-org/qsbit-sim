# Test the simulator

The test suite checks instruction results, cycle timing, control protocols and
device behavior. Tests that cross a module boundary assert timestamps and final
state as well as successful completion.

Run commands from the repository root. The
[build guide](building.md#enable-tests) shows how to enable tests.

## Run the fast suite

```sh
cmake --preset gcc-ninja -DCMAKE_PREFIX_PATH="$HOME/.local/systemc" -DBUILD_TESTING=ON
cmake --build --preset gcc-ninja --parallel
ctest --test-dir build-gcc -L fast --output-on-failure
```

List registered tests with `ctest --test-dir build-gcc -N`.
Select a test by name with `-R`, for example:

```sh
ctest --test-dir build-gcc -R '^control\.' --output-on-failure
```

Optional dependencies and build flags determine which tests are registered.

## What each test family checks

| Test family | Main assertions |
| --- | --- |
| `core.*` | RV32I effects, legal encodings, image access, mailboxes and memory service. |
| `control.*` | Atomic admission, manifests, queue bounds, deadlines, result slots and predicates. |
| `protocol.*` | Held operations, capacity faults, resources, readout timing, reset, overflow and token history. |
| `systemc.*` | ELF execution, pipeline and feedback timing, reset, CLI behavior and process-registration order. |
| `adapter.*` | Replacement CPU construction through `ICpuCycleModel` and session reset. |
| `docs.*` | Documentation links, checked declarations and registered test references. |

Each [module page](modules/README.md) names its relevant CTest entries and
describes what those tests establish.

## Timing regressions

When changing a protocol, check the boundary case that distinguishes the old
and new behavior. The existing suite includes:

- Two APPENDs at one cursor, including ADVANCE(0), followed by one complete admission.
- Requests published exactly on a receiver edge, and admission while a full
  queue fires. The receiver cannot consume a newly published request on that edge or reuse
  a slot freed by that edge's firing.
- Empty timing queues during CPU feedback waits. The timer continues and
  late groups fail without shifting their deadlines.
- Taken branches and older faults that discard younger control instructions.
- Overlapping resource intervals, adjacent intervals and same-target sampling
  collisions. Invalid batches must not partially change device state.
- Delayed discriminator arms, zero discriminator delay, independent CPU and
  fast-result crossings, and exact-token history eviction.
- Reset at clock and physical boundaries, stale completions and result-slot reuse.
- END while physical work or fast-feedback credits remain pending.

Registration-order tests run equivalent scenarios with reversed SystemC process
registration and compare the resulting trace and state.

## SystemC test processes

Run each independent SystemC scenario in a fresh executable or child process.
A normal C++ fixture cannot assume it can rewind elaboration or simulation time.
Keep pure ISA and transition tests independent of SystemC where possible.

Use deterministic inputs and seeds. Assert event times, required ordering,
final registers or memory, and the stop reason. When a comparison fails, report
the seed and first differing event so the run can be reproduced.

## Optional backend tests

Enable `QSBIT_PYTHON_BACKENDS` and the selected numerical test options described
in [building](building.md#cmake-options).
`python.plugin` checks adapter loading without numerical packages.
`numerical.*` checks live Aer Bell correlations and feedback, pulse inversion,
simultaneous drives, and unsupported operations.

The pulse tests include simultaneous noncommuting drives with an analytic
expected state. Matching a sequence of ideal gates would not establish that
joint pulse evolution is correct.

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

## Compare with CACTUS

CACTUS validation runs in a separate disposable project. It compiles a shared
workload into independent eQASM and RV32I-extension programs, runs both, and
compares corresponding control and device events on a common time grid.
qsbit-sim must execute its own program rather than consume the reference trace
as a schedule.

The fixture records both binaries, reference revision and patches, execution
entry point, actual clock periods and phases, reset/start timing, mappings and
measurement inputs. Compare absolute event times after one common-origin
normalization, with zero tick tolerance. Do not shift individual events.

Producer acceptance times and private queue occupancies can differ between CPU
models. Missing reference probes and unsupported features are coverage limits.
[ADR 0002](decisions/0002-reference-comparison-scope.md) defines the required
observations and known reference limitations.

All reference-specific translators, probes, fixtures and reports stay outside
this repository and its default CI workflow.

## CI, sanitizers and coverage

The [CI workflow](../.github/workflows/ci.yml) runs a core sanitizer build, a
bridge-only build, Aer tests, pulse tests and documentation checks. It checks
formatting and optional-dependency isolation and preserves diagnostic artifacts.
Compiler warnings are errors.

Use the [sanitizer build](building.md#sanitizers) to check address and
undefined-behavior errors. For line and branch coverage, use a separate build:

```sh
cmake -S . -B build-coverage -DCMAKE_PREFIX_PATH="$HOME/.local/systemc" \
  -DBUILD_TESTING=ON -DQSBIT_COVERAGE=ON
cmake --build build-coverage --parallel
ctest --test-dir build-coverage --output-on-failure
python3 tools/coverage.py --build build-coverage
```

Coverage artifacts stay under the build directory. Examine untested error and
timing paths; a global coverage percentage alone does not prove correctness.

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

These checks catch structural drift. Behavioral claims still need the relevant
simulator assertions. Changes to rendered pages or diagrams also need the
[website tests](website.md#run-website-tests).
