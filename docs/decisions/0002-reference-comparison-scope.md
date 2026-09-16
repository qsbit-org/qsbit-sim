# ADR 0002: Observable timing equivalence and reference defects

Date: 2026-09-17. Status: accepted; external acceptance runs are recorded separately.

The requested equivalence concerns final quantum-operation timing and TCU behavior.
The classical microarchitecture may differ. Therefore producer acceptance and private
queue occupancy are diagnostic probes, not equal-time assertions across two different
CPU pipelines. Each simulator must independently execute its own program. Required
common probes are label firing, device operation identity and start tick, measurement
result readiness, and CPU feedback visibility when the reference implements it.
Operation-end timing is checked against the selected device contract in native tests;
a reference that exposes no end event cannot certify that probe.

The pinned reference's binary measurement path has two source defects: incorrect
measurement bit masks and an opcode comparison that implicitly constructs a width-only
integer wrapper. Keep original-reference runs and failures. A separately named reference
variant may apply the minimal binary-measurement fixes, with its patch and executable
hash recorded externally. It must not change TCU, clock, device, or feedback delay code.
Unmodified assembly-mode feedback is an additional independent cross-check; it does not
substitute for a successful binary test. Reports must distinguish all reference variants.

Reference FIFO prefetch and final-group buffering require terminal waiting points.
These guards contain no quantum operations. A bounded reference observation window
must include every functional operation and feedback result, and report no queue error.
Its later guard-only points are outside the workload boundary comparison. Native QEND
must still drain completely. This establishes functional workload timing equivalence,
not equivalence of the reference's finite-program termination protocol.

Deterministic basis-state measurements may use live numerical backends on both sides.
This avoids injecting a reference trace or an expected schedule into qsbit-sim. For
probabilistic circuits, equal RNG seeds across different libraries do not imply equal
samples: validate state probabilities, correlations and timing separately. Scripted
measurements remain appropriate for native protocol tests and future external fixtures.

The reference's unimplemented fast-condition path cannot certify QAPPEND_IF. Native
exact-token history, same-edge exclusion, cancellation and delivery-credit tests cover
that contract. Reports list it as unsupported by the reference, never as a passing
reference comparison. Distributed synchronization remains explicitly unsupported in v1.
