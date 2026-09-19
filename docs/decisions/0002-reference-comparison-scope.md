# ADR 0002: Observable timing equivalence

Date: 2026-09-17. Status: accepted.

## Context

CACTUS and qsbit-sim use different instruction sets and classical pipelines.
The comparison must establish that independently executed programs produce the
same quantum-operation timing and supported TCU behavior. Internal CPU activity
cannot provide a common cycle-by-cycle reference.

## Required observations

Compare corresponding label firings, device-operation identities and start ticks,
measurement readiness, and CPU result visibility where the reference exposes
those events. Use one declared time origin; do not align individual events after
the run to remove discrepancies.

Producer acceptance and private queue occupancy help diagnose a mismatch, but
they need not occur at equal times in different CPU pipelines. Operation-end
timing is covered by native tests of the selected device contract. A reference
that exposes no end event cannot verify that observation.

## Reference variants

The pinned CACTUS binary-measurement path has two source defects: incorrect
measurement bit masks and an opcode comparison that constructs a width-only
integer wrapper. Preserve runs and failures from the unmodified reference.

A separately named variant may apply the minimal fixes, with its patch and
executable hash recorded in the external comparison workspace. It must leave
TCU behavior, clocks, device delays and feedback delays unchanged. Unmodified
assembly-mode feedback provides an additional cross-check; it does not replace
a successful binary-mode test. Every result identifies the reference variant.

## Workload boundaries

The reference's FIFO prefetch and final-group buffering require terminal waiting
points. These points contain no quantum operations. The observation window must
include every functional operation and feedback result, with no queue error.
Later waiting points fall outside the workload comparison.

Native QEND must still drain the simulator completely. The comparison therefore
verifies the functional workload's timing; the reference's finite-program
termination protocol has a separate boundary.

## Measurements and unsupported features

Deterministic basis-state measurements can use numerical backends on both
sides. Each simulator computes its own results without importing an expected
trace or schedule. For probabilistic circuits, equal random seeds in different
libraries do not imply equal samples. Check probabilities, correlations and
timing separately. Scripted measurements remain useful for native protocol tests.

The reference does not implement the fast-condition path and cannot verify
QAPPEND_IF. Native tests cover exact measurement matching, exclusion of results
that arrive on the firing edge, conditional cancellation and delivery credits.
Comparison results identify this limitation explicitly. Distributed
synchronization is unsupported in the initial qsbit-sim profile.

Comparison programs, patches, traces and reports remain outside the repository.
