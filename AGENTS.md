# Engineering Rules

## C++ and build

- Write repository content, comments, test names, and commit messages in English.
- Use C++20, the checked-in `.clang-format`, and the pinned dependency versions. Use standard CMake dependency discovery and pinned fallback sources. Keep builds offline once dependencies are provisioned. Treat compiler warnings as errors; run `clang-tidy` on changed C++ where available.
- Prefer value types, RAII, `enum class`, `std::span`, fixed-width integers at binary/protocol boundaries, and explicit ownership. Avoid raw owning pointers, hidden global state, and unchecked time or capacity arithmetic.
- Keep pure transition logic independent of SystemC when practical. Keep scheduler code in SystemC owners and avoid leaking SystemC types into pure interfaces. Give each mutable state object one owner; document reset, overflow, and teardown behavior.
- Use bounded storage for modeled hardware resources. Make backpressure and capacity faults observable instead of silently dropping work.

## SystemC processes and time

- Register processes during elaboration. Put `sensitive`, `dont_initialize()`, and any reset declaration immediately after the process they configure. Document what triggers each process, whether it runs during initialization, and how it resets.
- Use `SC_METHOD` for work that completes on each trigger. It must return without `wait()`; persist state in an owned object, not in method-local variables. Use `next_trigger()` only when dynamic sensitivity is intentional and documented.
- Use `SC_THREAD` when a process needs to suspend with `wait()` and resume with its call stack and local variables intact. Every continuing thread path must eventually wait; an infinite loop without `wait()` prevents the nonpreemptive kernel from running other processes. Use `SC_CTHREAD` only for a single-clock process that needs its clocked `wait()` semantics; it cannot wait on arbitrary events or time intervals.
- Set the global time resolution before constructing any nonzero `sc_time`, and check it at the simulation boundary. Use `sc_time` or checked integer ticks for simulated time; host execution time and delta-cycle count are not hardware cycles.
- Treat `notify()` as immediate, `notify(SC_ZERO_TIME)` as next-delta, and `notify(nonzero_time)` as a future simulation-time event. An `sc_event` carries no payload and has at most one pending notification; repeated timed notifications do not form a queue. Keep queued data in an owned channel or container and wake consumers separately.
- Remember that `sc_signal::write()` commits in the update phase: a read in the same evaluation phase sees the old value. Specify the edge and delta at which a consumer can observe a change. Use signals for signal semantics, and explicit bounded mailboxes when a multi-item transaction protocol is required.
- Never make externally visible behavior depend on which runnable process executes first at one simulation time. Define sampling, state commit, notification, and output visibility across coincident clock edges; use an explicit barrier or equivalent protocol where shared work needs all owners to finish.
- Keep reset and cancellation rules explicit. A reset must invalidate pending work or identify it by epoch; `sc_event::cancel()` removes a pending notification, not an already delivered one. Do not assume that `sc_stop()` permits another simulation run in the same process.
- Keep processes small. A synchronous C++ or Python backend call holds the SystemC scheduler; keep its host duration outside simulated timing and document where that call occurs. `wait()` advances simulation scheduling, not the host computation.

## Tests and diagnostics

- Test specified behavior and observable interfaces. Do not enforce terminology blacklists, prose, private names, incidental call sequences, or fixed page structure. Check machine-readable fields and meaningful events instead of entire logs.
- Add or update a focused test for each behavior change; retain a minimal reproducer for each bug fix. Test pure transitions without SystemC where possible. Use integration tests for interactions between components and clocks, and compiler-to-simulator execution; avoid repeating component assertions at every layer.
- Cover instruction semantics, quantum results, event timing and measurement feedback. Include empty and full queues, overflow, invalid inputs, resource conflicts, reset, stale or duplicate results, coincident edges, and altered process registration order where relevant.
- Establish expected results from specifications, independent calculations or reference implementations. Self-generated golden files detect changes but do not independently establish correctness. For paper or hardware comparisons, record the source, conditions, measurement boundaries and tolerances.
- Use mocks to control inputs and exercise failure paths. Test quantum evolution with a numerical backend against independent expectations. Set numerical tolerances and sampling criteria before evaluating results; retain seeds, configurations, failing inputs and the first divergence for reproduction.
- Run each independent SystemC scenario in a fresh executable or child process. Do not assume that elaboration or simulation time can be reset in an ordinary C++ test fixture.
- Assert specified timestamps, required event ordering, final state and stop or fault reason; process exit status alone is insufficient. Benchmark host runtime and memory separately using repeated measurements in a controlled environment, not tight wall-clock thresholds in ordinary CI.
- Register tests with CTest and set timeouts. Before reporting completion, run affected tests, the fast suite, small integration cases, formatting checks and sanitizers relevant to the changed boundary. Schedule longer randomized, numerical and model-validation runs separately. Use coverage to find untested behavior, not as proof of correctness.

## Repository hygiene

- The project is in rapid iteration. Do not preserve backward compatibility or add compatibility shims; refactor interfaces when needed and update their consumers together.
- Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) for every commit message, such as `fix(simulator): evolve the final state to the stop tick`. The configured `commit-msg` hook and CI must pass.
- Keep review notes, agent reports, audit findings, generated traces, local dependencies, and temporary visualizations out of tracked files. Use ignored local directories or an external workspace. Incorporate accepted findings into the relevant code, test, or `docs/` contract.
- Put design decisions, timing contracts, instruction semantics, and feature scope in `docs/`, not in this file. Update those documents together with behavior changes.
- Write repository documents as direct technical guidance. Omit commentary about how a document was drafted, which source inspired its wording, or whether its rules are project conventions.

## Documentation and dependency management

- Lead README with the shortest supported build and a runnable example. Put advanced options, backend contracts, and contributor workflows in linked documents.
- Use ordinary CMake configure, build, and CTest commands. Keep custom provisioning scripts and machine-specific paths out of the normal user workflow.
- Separate required build tools, test tools, and optional runtime backends. A default C++ build must not install or require Python quantum packages.
- Use a local `.venv` for optional Python dependencies, created with Python venv or uv. User commands must use the selected interpreter without hard-coded minor versions or temporary environment paths. CI may select explicit Python versions to test compatibility; dependency installation must use that selected interpreter.
- Keep package requirements and compatibility constraints in dependency manifests, and reproducible resolutions in lock files. Link those files instead of duplicating dependency/version lists in prose.
- Provide explicit opt-in installation for each backend. Import optional adapters lazily and allow external adapters through documented interfaces.
- Keep examples copyable from a stated working directory, identify their output files and expected results, and verify them using the current build tree.
- Document only implemented behavior. Update examples, CLI help, configuration contracts, and build instructions together when their interfaces change.
- Keep C++ declarations in the source-excerpt blocks in `docs/cpp-interfaces.md`; refresh them with `python tools/check_docs.py --build BUILD_DIRECTORY --write` after interface changes. Do not invent fields or signatures in prose.
- Give each module page a `**CTest:**` entry naming registered baseline tests and describe what their assertions establish. Identify optional tests and unsupported features explicitly. Do not substitute planned tests for implemented coverage or hard-code aggregate test counts.
- Treat documentation checks as required tests. Run `ctest --test-dir BUILD_DIRECTORY -L documentation --output-on-failure`; update behavioral assertions whenever a timing or protocol contract changes. Passing link and excerpt checks does not establish behavioral correctness.
- Build the website with Sphinx warnings treated as errors after changing site content or extensions. Keep Doxygen declarations and observable trace examples tied to the current checkout; run the optional `website` CTest label for diagram or player changes.
- Label derived clocks and last-observed trace values explicitly. Do not reconstruct undocumented internal state in the browser. Keep generated HTML, XML, traces and browser captures in ignored build directories.
