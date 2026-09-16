# Engineering and Verification Plan

**Status:** Phase 1 verification requirements agreed; fixtures and selected backend versions remain to be pinned, 2026-09-16.

## Coding and SystemC rules

Use C++20, CMake, and a pinned Accellera SystemC release. Keep the RV32I ISA library free of SystemC dependencies and isolate pure state transitions from scheduling processes. Represent architectural values with fixed-width integers and simulated event times with checked integer ticks or `sc_time` at the SystemC boundary. Give each mutable state object one owner; define construction, reset, overflow, and teardown behavior. Use the checked-in `.clang-format`, enable compiler warnings as errors in CI, and run `clang-tidy` on changed C++ where available.

Use `SC_METHOD` only for nonblocking behavior and `SC_THREAD` when `wait()` is required. Document sensitivity, reset, and which edge observes a handshake. Avoid modeling hardware queues with unbounded containers. Do not rely on incidental SystemC delta-cycle order for a hardware-visible result: define and test the same-timestamp sampling, acceptance, state commit, and output-visibility rule. Allow alternate order only for diagnostic events whose order is explicitly outside the contract. Log structured machine-readable trace records for CPU retirement, command acceptance, queue changes, TCU dispatch, readout, feedback, errors, and stop reason.

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
| CPU adapter component | Held request under backpressure, acceptance at a defined edge, one-time side effect, memory response, feedback visibility, and same-time edge ordering. | No command loss or duplication; cycle and boundary events match the protocol. A replacement adapter runs the same contract tests. |
| TCU and device component | Queue full and almost-full, two independent ports at one target time, same-port conflict, equal-time tie rule, missed deadline, clock crossing, readout and reset. | Exact event time, ordering and status; bounded resource state remains valid. |
| End-to-end scripted use case | RV32I-extension ELF issues controls, waits, measures and branches for both deterministic outcomes. | Full boundary trace, final memory signature and stop reason match the fixture. |
| External CACTUS differential gate | One ISA-neutral workload produces independent CACTUS eQASM and qsbit-sim RV32I-extension binaries; run matched configuration with scripted measurement streams. | Event count, operation, target, order, correlation and absolute time are identical with zero common-grid-tick tolerance at every required probe. Missing probes and unsupported mappings fail explicitly. |
| Device adapter contract | Run a circuit-level live-measurement prototype and a small pulse-level prototype; exercise unsupported capabilities, reset, qubit ordering, overlapping drives, and host-time isolation. | Each adapter passes its declared capability tests; unsupported operations reject clearly. Backend compute duration never changes simulated event time. |
| Architecture conformance | Applicable RV32I cases from the [RISC-V Architectural Certification Tests](https://github.com/riscv/riscv-arch-test) through a target adapter. | Applicable tests pass; unsupported privilege or platform requirements are identified, not silently skipped. |
| Differential and randomized | Compare retired state with Spike or another independent ISA reference using seeded programs. | No architectural mismatches; retain seed, program, configuration and first differing retirement. |

The CACTUS gate is implemented entirely in a separate, disposable validation project. It calls the public qsbit-sim interface and reads its versioned generic trace; all CACTUS-specific probes, translators, workloads, fixtures, comparison scripts, and artifacts stay outside this repository. It is neither a core build dependency nor a default CI job. Removing that project must leave qsbit-sim source, build files, and tests unchanged. The CACTUS gate has priority over hand-written golden schedules for a Phase 1 timing claim. Its fixture records the exact CACTUS commit, execution entry point, actual clock periods and phases, reset release, codeword and device configuration, deterministic measurements, both binaries, and a versioned trace schema. Start with single commands and waits, then independent simultaneous ports, queue pressure, both feedback branches, repeated measurements, and a longer program. On failure, print the first divergent boundary event and the nearest CPU stall and queue-state records. Compare absolute post-reset time after a single common-origin normalization; do not shift individual events.

## CI and test execution

Keep pure ISA tests independent of SystemC. Run each SystemC scenario in a fresh executable or child process; a normal C++ fixture must not assume elaborated modules and simulation time can be rewound. Register tests with CTest, give each scenario a timeout, and use deterministic seeds printed on failure. Separate fast tests from slower architecture, random, and numerical-backend jobs; run the CACTUS differential suite from the external validation project.

The initial CI gate builds Debug and Release, checks formatting, treats warnings as errors, runs fast CTest, and runs AddressSanitizer and UndefinedBehaviorSanitizer where supported. Add coverage reports for ISA decode, extension errors, TCU conflict handling, and feedback; a single global coverage percentage is not an acceptance criterion. Phase 1 completion additionally requires the applicable architecture tests, CACTUS differential suite, and two backend-adapter contract prototypes. Preserve test binaries, configuration and event traces as reviewable artifacts. A successful process exit alone is never sufficient.

## References

- [Accellera SystemC reference implementation](https://github.com/accellera-official/systemc) provides API behavior, examples, and regression tests.
- [RISC-V Architectural Certification Tests](https://github.com/riscv/riscv-arch-test) provide an ISA conformance source for declared target configurations.
- [RISC-V assembly manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc) and [GNU `.insn` formats](https://sourceware.org/binutils/docs/as/RISC_002dV_002dFormats.html) document existing custom-instruction assembly support.
- [CMake CTest documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html) defines test registration and execution. The fresh-process convention is a qsbit-sim decision based on the SystemC kernel lifecycle.
