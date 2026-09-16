# Engineering and Verification Plan

**Status:** Discussion draft, 2026-09-16

## Coding rules

Use C++20, CMake, and the Accellera SystemC reference implementation. Pin the tested SystemC release rather than using an unbounded latest version. Put portable RV32I semantics in a library without a SystemC dependency. Keep simulation modules thin and isolate clocked behavior from pure algorithms. Use `sc_time` for simulation scheduling, fixed-width integers for architectural values, and explicit conversions at their boundary. Choose one state owner per module, initialize it in construction or reset, and document reset semantics.

Use `SC_METHOD` only for nonblocking logic that cannot suspend. Use `SC_THREAD` when `wait()` is required. Avoid ambiguous same-timestamp behavior: tests should assert ordering where the protocol guarantees it and allow either order where it does not. Avoid unbounded `sc_fifo` use for hardware queues. Report model errors through structured status and SystemC reports, with stable machine-readable trace records. Run formatting and static analysis in CI.

These rules are project decisions informed by the [SystemC language standard and reference implementation](https://systemc.org/resources/standards/) and [Accellera's Common Practices project](https://github.com/accellera-official/systemc-common-practices); they are not a verbatim Accellera style guide. The Common Practices repository provides reusable transaction, configuration, and reporting patterns, but it does not define all project-specific process or test conventions.

## Open-source RV32I references

| Project | Relevant use | Integration assessment |
| --- | --- | --- |
| [Ripes](https://github.com/mortbopet/Ripes) | Five-stage processor datapath and hazard behavior reference. | Useful design and test oracle; its graphical application architecture is not a direct SystemC component. Check license before copying code. |
| [skyzh RISCV-Simulator](https://github.com/skyzh/RISCV-Simulator) | C++ RV32I pipeline branch and instruction-level examples. | Small enough to inspect; branch-specific code and maintenance state require review before reuse. |
| [rv32emu](https://github.com/sysprog21/rv32emu) | Functional RV32I execution and broad instruction support. | Candidate independent architectural oracle, not a cycle pipeline substitute. |
| [Spike](https://github.com/riscv-software-src/riscv-isa-sim) | Established functional ISA reference. | Prefer differential testing or an optional external adapter; integration is larger than a small embedded core. |

Recommendation: implement the project-owned RV32I pipeline and its plain-C++ ISA core. Reuse open-source code selectively only after API, license, and test review. This meets the requirement to implement and understand the initial pipeline while leaving an external CPU backend feasible.

## Test layers and gates

| Layer | Tests | Required result |
| --- | --- | --- |
| ISA unit | Decode fields, immediates, sign extension, arithmetic overflow, branches, loads, stores, alignment, zero register, illegal encodings. | Exact architectural state and trap result per case. |
| Pipeline unit | Forwarding, load-use stall, control hazard flush, simultaneous memory and command pressure, retirement order. | Cycle trace and final architectural state match the specified pipeline model. |
| TCU unit | Timestamp order, equal-timestamp tie rule, queue full and empty, lane occupancy, late command, reset, feedback correlation. | Exact event ticks, order, status, and no loss or duplication. |
| Component integration | CPU command adapter, TCU, scripted device, and feedback path. | Trace matches a golden event schedule; both measurement outcomes run. |
| End-to-end use case | Bare-metal RV32I program emits timed controls and branches on feedback. | Final memory signature and complete trace match expectations. |
| Architecture tests | Supported RV32I subset from the [RISC-V Architectural Certification Tests](https://github.com/riscv/riscv-arch-test) through an appropriate target adapter. | All applicable tests pass; unsupported privileged features are declared, never quietly skipped. |
| Differential and randomized | Compare retired architectural state with Spike or another independent reference using seeded programs. | No mismatches; failing seed and program are retained. |

Run pure unit tests without SystemC. Give each SystemC test case its own executable or fresh child process because elaborated modules and simulation time are not generally reset to an initial state by a normal C++ test fixture. Register tests with CTest. Use a small timeout for every scenario to detect deadlock. Use deterministic fake devices and seeded random streams. Keep slow architecture and random tests in a separate CI tier.

The initial CI gate should compile Debug and Release builds, run formatting checks, compile with warnings enabled, run fast CTest, and run AddressSanitizer plus UndefinedBehaviorSanitizer jobs where supported. Coverage reports should show untested branches in ISA decode, TCU error handling, and feedback paths; a single overall percentage is not an acceptance criterion. Add a trace-schema compatibility test when the first external adapter is introduced.

## References for testing approach

- [Accellera SystemC reference implementation](https://github.com/accellera-official/systemc) includes examples and a regression suite; it is the source for API behavior and regression patterns.
- [RISC-V Architectural Certification Tests](https://github.com/riscv/riscv-arch-test) are the instruction-set conformance source for supported target configurations.
- [CMake CTest documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html) defines test registration and execution. The separate-process SystemC test convention is a project decision based on the simulation kernel lifecycle.
