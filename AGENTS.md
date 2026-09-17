# Engineering Rules

## C++ and build

- Write repository content, comments, test names, and commit messages in English.
- Use C++20, the checked-in `.clang-format`, and the pinned dependency versions. Keep configure and build independent of the network after dependencies are provisioned. Treat compiler warnings as errors; run `clang-tidy` on changed C++ where available.
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
- Never make externally visible behavior depend on which runnable process executes first at one simulation time. Define sampling, state commit, notification, and output visibility across coincident clock edges; use an explicit barrier or equivalent protocol where shared work needs all owners to finish. Test with altered process registration order when this risk exists.
- Keep reset and cancellation rules explicit. A reset must invalidate pending work or identify it by epoch; `sc_event::cancel()` removes a pending notification, not an already delivered one. Do not assume that `sc_stop()` permits another simulation run in the same process.
- Keep processes small. A synchronous C++ or Python backend call holds the SystemC scheduler; keep its host duration outside simulated timing and document where that call occurs. `wait()` advances simulation scheduling, not the host computation.

## Tests and diagnostics

- Add a focused test for every behavior change and a regression test for every bug fix. Test pure transitions without SystemC where possible; add an integration test when behavior crosses an owner or clock boundary.
- Run each independent SystemC scenario in a fresh executable or child process. Do not assume that elaboration or simulation time can be reset in an ordinary C++ test fixture.
- Assert simulated timestamps, event order where specified, boundary state, final state, and stop or fault reason. Process exit status alone is insufficient. Cover same-timestamp events, reset, queue limits, and reordered process registration where relevant.
- Use deterministic seeds and report the seed and first divergent event on failure. Register tests with CTest and set timeouts. Keep fast tests separate from longer randomized or numerical tests.
- Before reporting a change complete, run the affected tests and the fast CTest suite; run formatting and relevant sanitizer or integration checks when the change touches their boundary. Preserve reproducible failure inputs and traces outside tracked source.

## Repository hygiene

- Keep review notes, agent reports, audit findings, generated traces, local dependencies, and temporary visualizations out of tracked files. Use ignored local directories or an external workspace. Incorporate accepted findings into the relevant code, test, or `docs/` contract.
- Put design decisions, timing contracts, instruction semantics, and feature scope in `docs/`, not in this file. Update those documents together with behavior changes.
- Write repository documents as direct technical guidance. Omit commentary about how a document was drafted, which source inspired its wording, or whether its rules are project conventions.
