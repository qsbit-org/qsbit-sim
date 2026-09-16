# Engineering and Verification Plan

**Status:** executable verification suite, 2026-09-17. Dependency versions are pinned; [implementation.md](implementation.md) describes the implemented profile. [ADR 0002](decisions/0002-reference-comparison-scope.md) records reference limitations.

## Coding and SystemC rules

Use C++20, CMake, and a pinned Accellera SystemC release. Keep the RV32I ISA library free of SystemC dependencies and isolate pure state transitions from scheduling processes. Represent architectural values with fixed-width integers and simulated event times with checked integer ticks or `sc_time` at the SystemC boundary. Give each mutable state object one owner; define construction, reset, overflow, and teardown behavior. Use the checked-in `.clang-format`, enable compiler warnings as errors in CI, and run `clang-tidy` on changed C++ where available.

Use `SC_METHOD` only for nonblocking behavior and `SC_THREAD` when `wait()` is required. Document sensitivity, reset, and which edge observes a handshake. Avoid modeling hardware queues with unbounded containers. Do not rely on incidental SystemC delta-cycle order for a hardware-visible result: define and test the same-timestamp sampling, acceptance, state commit, and output-visibility rule. Allow alternate order only for diagnostic events whose order is explicitly outside the contract. Log structured machine-readable trace records for CPU retirement, producer acceptance, group admission, queue changes, label firing, readout, feedback, errors, and stop reason.

These are project conventions informed by the [SystemC reference implementation](https://github.com/accellera-official/systemc) and [Accellera Common Practices](https://github.com/accellera-official/systemc-common-practices); neither is presented as a verbatim style guide for qsbit-sim.

## Open-source RV32I references

| Project | Relevant use | Integration assessment |
| --- | --- | --- |
| [Ripes](https://github.com/mortbopet/Ripes) | Five-stage datapath and hazard behavior. | Useful independent cycle examples; its graphical model is not a drop-in SystemC component. Review license before copying. |
| [rv32emu](https://github.com/sysprog21/rv32emu) | Functional RV32 instruction execution. | Candidate architectural-state oracle, not a cycle-pipeline substitute. |
| [Spike](https://github.com/riscv-software-src/riscv-isa-sim) | Established ISA simulator. | Prefer differential testing or an optional CPU adapter after interface review. |
| [RISCV-VP](https://github.com/agra-uni-bremen/riscv-vp) | RV32 semantics and ELF loading within a SystemC platform. | Useful reference for program loading and adapters; instruction-based timing is not automatically the selected pipeline profile. |

The initial pipeline belongs to qsbit-sim. Borrowing source requires license, maintenance, API, and test review. A future external CPU can be a functional adapter, or it can declare a tested timing profile; architectural-state agreement alone does not establish cycle equivalence.

## Test layers and release gates

| Layer | Tests | Required result |
| --- | --- | --- |
| ISA and ELF unit | Decode every supported RV32I class and extension mask; immediates, sign extension, arithmetic overflow, branches, alignment, zero register, illegal encodings, ELF mapping and entry. | Exact architectural effect and error. Unknown extension words never become no-ops. |
| Toolchain contract | Assemble each custom instruction with pinned GNU or LLVM `.insn` macros, link an RV32 ELF, disassemble it, load it, then decode and execute it. | The versioned ISA specification, emitted machine word, disassembly, and simulator behavior agree. Unexpected compression or relaxation is detected. |
| Pipeline unit | Forwarding, load-use stall, branch flush, memory wait, extension blocking, retirement order, reset at each stage. | Cycle trace and architectural state match the selected profile. |
| CPU adapter component | Oldest non-speculative publication, producer acceptance versus group admission, held request and one-time retirement, result-read flush, and memory response. | No instruction discarded by a pipeline flush emits a control event; two scalar APPENDs can complete one group without deadlock; retirement follows each operation's completion rule. |
| TCU and device component | Intervals and member manifests, label broadcast, zero-wait coalescing, atomic admission, empty-stream recovery, late admission, old-state queue credits, per-port firing width, fixed output latency, conflicts and drain. | Exact trigger and physical-output ticks; idle unmentioned ports remain valid; faults cause no partial batch; empty queues never shift the logical timeline. |
| End-to-end scripted use case | RV32I-extension ELF issues controls, waits, measures and branches for both deterministic outcomes. | Full boundary trace, final memory signature and stop reason match the fixture. |
| External CACTUS differential gate | One ISA-neutral workload produces independent CACTUS eQASM and qsbit-sim RV32I-extension binaries; run matched configuration with deterministic scripted streams or deterministic live basis-state measurements. | Event count, operation, target, order, correlation and absolute time are identical with zero common-grid-tick tolerance at every required probe. Missing probes and unsupported mappings fail explicitly. |
| Device adapter contract | Run a circuit-level live-measurement prototype and a small pulse-level prototype; exercise unsupported capabilities, reset, qubit ordering, overlapping drives, and host-time isolation. | Each adapter passes its declared capability tests, including one chronological backend owner and joint same-time batching; unsupported operations reject clearly. Backend compute duration never changes simulated event time. |
| Architecture conformance | Applicable RV32I cases from the [RISC-V Architectural Certification Tests](https://github.com/riscv/riscv-arch-test) through a target adapter. | Applicable tests pass; unsupported privilege or platform requirements are identified, not silently skipped. |
| Differential and randomized | Compare retired state with Spike or another independent ISA reference using seeded programs. | No architectural mismatches; retain seed, program, configuration and first differing retirement. |

The CACTUS gate is implemented entirely in a separate, disposable validation project. It calls the public qsbit-sim interface and reads its versioned generic trace; all CACTUS-specific probes, translators, workloads, fixtures, comparison scripts, and artifacts stay outside this repository. It is neither a core build dependency nor a default CI job. Removing that project must leave qsbit-sim source, build files, and tests unchanged. The CACTUS gate has priority over hand-written golden schedules for a Phase 1 timing claim. Its fixture records the exact CACTUS commit, execution entry point, actual clock periods and phases, reset release, codeword and device configuration, deterministic measurements, both binaries, and a versioned trace schema. Start with single commands and waits, then independent simultaneous ports, queue pressure, both feedback branches, repeated measurements, and a longer program. On failure, print the first divergent boundary event and the nearest CPU stall and queue-state records. Compare absolute post-reset time after a single common-origin normalization; do not shift individual events.

## CI and test execution

The baseline protocol in [module-architecture.md](module-architecture.md) requires these focused regression scenarios when implemented:

| Scenario | Required result |
| --- | --- |
| Two APPENDs planned for one logical TCU cycle, then ADVANCE | Both APPENDs retire after entering the same bounded open group; ADVANCE seals it, one complete group is admitted, and both ports fire on the planned TCU edge. |
| Oversized staging group, excessive per-port firing width, or insufficient total destination capacity | Immediate typed fault, not a stall depending on a later instruction. |
| Configured TCU start before the first real point is ready | The empty origin may be skipped; a future point succeeds only if admitted before its original due edge. |
| Empty TCU stream during measurement feedback | Timer continues; a timely later point succeeds and an expired one fails without rebasing. |
| Request published on a receiver edge, and a full queue firing on the admission edge | Strictly later visibility; freed-slot credit is usable on the following edge. |
| Reversed SystemC module registration and runnable order | Identical hardware-visible trace and state. |
| Older taken branch or memory fault | No younger speculative control request reaches producer staging. |
| Two delayed pulses reserve overlapping exclusive intervals | Entire conflicting launch batch fails before any member begins; adjacent half-open intervals succeed. |
| Overlapping pulse drives delivered in opposite port order | One joint state evolution per interval and the same physical result. |
| Unknown, duplicate, consumed, or old-epoch measurement token | Explicit error or specified stale-epoch discard; no overwrite of another result-slot generation. |
| Late discriminator arm and zero discriminator processing delay | Result readiness uses both acquisition end and arm tick; CPU delivery still follows crossing latency. |
| Different CPU and fast-feedback latencies | Independent delivery and credit release; a later pending measurement does not suppress fast-condition history. |
| Session reset coincident with a start, pulse boundary or result completion | Reset dominates; no old-epoch callback can create new-epoch state. |
| END while pulses or results are outstanding | The simulator drains physical work and every enabled feedback crossing, including a slower fast-condition path, before reporting success. |
| Same-target instantaneous gate and measurement on one tick | The full boundary batch is rejected before quantum-state mutation. |

Keep pure ISA tests independent of SystemC. Run each SystemC scenario in a fresh executable or child process; a normal C++ fixture must not assume elaborated modules and simulation time can be rewound. Register tests with CTest, give each scenario a timeout, and use deterministic seeds printed on failure. Separate fast tests from slower architecture, random, and numerical-backend jobs; run the CACTUS differential suite from the external validation project.

The initial CI gate builds Debug and Release, checks formatting, treats warnings as errors, runs fast CTest, and runs AddressSanitizer and UndefinedBehaviorSanitizer where supported. Add coverage reports for ISA decode, extension errors, TCU conflict handling, and feedback; a single global coverage percentage is not an acceptance criterion. Phase 1 completion additionally requires the applicable architecture tests, CACTUS differential suite, and two backend-adapter contract prototypes. Preserve test binaries, configuration and event traces as reviewable artifacts. A successful process exit alone is never sufficient.

## References

- [Accellera SystemC reference implementation](https://github.com/accellera-official/systemc) provides API behavior, examples, and regression tests.
- [RISC-V Architectural Certification Tests](https://github.com/riscv/riscv-arch-test) provide an ISA conformance source for declared target configurations.
- [RISC-V assembly manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc) and [GNU `.insn` formats](https://sourceware.org/binutils/docs/as/RISC_002dV_002dFormats.html) document existing custom-instruction assembly support.
- [CMake CTest documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html) defines test registration and execution. The fresh-process convention is a qsbit-sim decision based on the SystemC kernel lifecycle.

## Executable test entry points

- `core.*`: RV32I arithmetic and control, encoding validation, ELF and raw memory, strict-edge mailboxes and memory service.
- `control.*`: atomic manifests and queue credits, empty-stream deadlines, scoreboard and fast predicates.
- `protocol.*`: held producer operations, staging and slot limits, resource conflicts, delayed discriminator arm, overflow rollback, reset and exact-token history.
- `systemc.*`: ELF use cases, registration-order invariance and 51 fresh-process pipeline, reset, clock, feedback and CLI scenarios.
- `adapter.*`: CPU factory injection through ICpuCycleModel, including session reset.
- `numerical.*`: real Aer Bell-state correlation and feedback, pulse inversion, full-device simultaneous drives, analytic simultaneous noncommuting drives and unsupported-capability rejection.
- `reference.rv32_random`: 32 deterministic seeds; compare every retired register state and PC plus final memory against Unicorn 2.1.4.
- `reference.rv32_architecture`: 38 pinned RV32I architecture-test bodies; compare retirements and memory signatures against Unicorn. One privileged-trap alignment case is explicitly excluded; native tests check the typed alignment fault. This is not an official certification result.

The architecture-test adapter replaces platform entry and exit, and uses RV32I NOP padding in the upstream address-load helper. It does not modify generated instruction-test bodies. The source revision is pinned in the runner. Artifacts stay in the build tree.

Run Debug and Release suites with Python backends, plus a Python-free ASan/UBSan build. Leak detection is disabled for the SystemC process lifecycle; address and undefined-behavior errors remain fatal. The CI workflow preserves failure traces. No CACTUS tooling is part of this workflow.

For focused line and branch evidence, configure a separate build with `-DQSBIT_COVERAGE=ON`, run its CTest suite, then run `python tools/coverage.py --build BUILD_DIRECTORY`. The generated gcov JSON and summary remain in that build directory. Coverage reports complement architectural assertions; no line-coverage percentage proves timing correctness.
