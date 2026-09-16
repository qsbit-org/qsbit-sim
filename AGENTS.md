# Agent Instructions

## Language and scope

- Write all repository content, code comments, tests, commit messages, and issue text in English.
- Treat `docs/high-level-design.md` as a proposal until its open decisions are resolved. Record a decision in that document or a dedicated architecture decision record before changing an architectural contract.
- Implement a C++20 and SystemC simulator. The initial classical engine must implement RV32I instruction semantics and a cycle-level pipeline. Keep its integration boundary independent of its concrete implementation so another RISC-V simulator can replace it later.
- Preserve QuMA's timing queue, per-port label-tagged event queues, deterministic-domain timer, and label-broadcast trigger semantics; see `docs/module-architecture.md`. Do not replace them with an absolute-time priority queue. Follow the timing-control principles described in the QuMA literature. Do not copy CACTUS internals as an architectural constraint. Phase 1 must compare independently executed equivalent workloads against CACTUS at the quantum-control boundary with zero common-grid-tick time difference.
- Keep every CACTUS-specific comparator, probe, translation script, workload, fixture, configuration, and result artifact in a separate disposable validation project. The simulator may expose a generic versioned trace through its public interface, but it must have no CACTUS-specific code path or build dependency. Removing the validation project must not require editing this repository.
- Treat MMIO-only and single-lane demonstrations as M0 smoke tests, not Phase 1 completion. Phase 1 uses RV32I extension instructions and validates both circuit-level and small-system pulse-level backend adapters.

## Source and dependency discipline

- Prefer the RISC-V unprivileged ISA specification and Accellera SystemC documentation for behavior. Use third-party simulators as references or optional adapters, and review license, maintenance, and API stability before importing source.
- Pin dependency versions. Keep the core build independent of a network connection after dependencies are provisioned.
- The simulator loads RV32 ELF or explicit raw machine code; it does not parse assembly text. Use an existing assembler with `.insn` for custom instructions. Manage assembler integration separately and share a versioned ISA encoding and semantics contract.
- Do not expose SystemC types in the RV32I ISA library or public CPU adapter contract. Keep SystemC in simulation modules and adapters.
- Use fixed-width integer types at ISA and protocol boundaries. Define time units, byte order, reset behavior, overflow behavior, and queue capacity explicitly.

## C++20 and SystemC implementation

- Apply the checked-in `.clang-format`; enable compiler warnings and treat new warnings as errors in CI. Use `clang-tidy` for changed C++ code where available.
- Prefer value types, RAII, `enum class`, `std::span`, and explicit ownership. Avoid raw owning pointers and hidden global state.
- Keep each SystemC process small and assign one clear owner to each piece of mutable state. Do not call `wait()` from `SC_METHOD`. Use `SC_THREAD` for blocking protocol behavior and `SC_METHOD` for nonblocking combinational or clocked state transitions. Logical substages within a clock-domain owner do not each require a separate process. Document process sensitivity and reset behavior.
- Model externally visible event times with `sc_time` and an explicit simulation time resolution. Do not infer hardware timing from host wall-clock time or delta cycles.
- Distinguish producer-operation acceptance from atomic TCU group admission; follow the sealing, strict-edge visibility, empty-stream, backend batching, and epoch-reset rules in `docs/module-architecture.md`.
- Use bounded queues where hardware backpressure matters. Make overflow and underflow observable. Specify same-time event order, cross-domain visibility, exact acceptance and one-time side effects; do not let SystemC delta-cycle order decide hardware behavior. Log structured trace events for instruction retirement, queue operations, feedback, and timed output.

## Testing requirements

- Every behavior change needs a focused unit test and, where it crosses a component boundary, an integration or use-case test. A bug fix needs a regression test that fails before the fix.
- Keep pure RV32I decode and instruction semantics tests independent of SystemC. Test pipeline hazards and retirement separately from ISA correctness.
- Run each SystemC scenario in a fresh test executable or process. Do not assume simulation time or elaborated module state can be reset in the same process.
- Assert event order, timestamps, queue state, final architectural state, and stop reason. A successful process exit alone is insufficient.
- Use deterministic seeds and print the seed on failure. Keep fast unit tests separate from longer randomized and RISC-V architecture tests.
- Register tests with CTest. Core CI must run formatting, build, unit tests, component tests, use-case tests, and sanitizer jobs where supported. Run CACTUS differential acceptance from the separate validation project.
- Do not claim a feature complete until the tests in `docs/engineering-and-testing.md` for that feature pass. Phase 1 timing claims require the CACTUS differential gate; adapter-extensibility claims require both circuit-level and pulse-level contract tests.

## Change workflow

Keep review working notes, agent review reports, audit findings, and review checklists out of tracked repository content. Store local review artifacts under the ignored `docs/reviews/` directory or outside this repository. Incorporate accepted findings into the relevant design document, decision record, code, or tests; do not link committed documents to ignored review artifacts.

1. Read the relevant architecture contract and cite the exact behavior being implemented.
2. Make the smallest coherent change with tests.
3. Run the affected tests, then the full fast CTest suite before reporting completion.
4. State what changed, what was tested, and any remaining limitation.
