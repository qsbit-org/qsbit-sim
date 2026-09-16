# Module Architecture Review — 2026-09-16

**Scope:** design review of `docs/module-architecture.md` at commit `16a2030`, with consistency checks against the HLD and verification plan. Three independent reviewers examined paper fidelity, event timing and progress, and module ownership and device behavior. The revised document received a second targeted review. This is a documentation review, not evidence of simulator execution or CACTUS timing equivalence.

## Findings and resolutions

| Finding | Failure example | Revised contract |
| --- | --- | --- |
| Producer retirement and atomic admission form a deadlock | First codeword waits for group admission, preventing a later codeword or wait from sealing that group. | APPEND retires on bounded producer acceptance; ADVANCE and FLUSH wait on separate group admission. A result read and END flush explicitly. |
| Oversized requests can wait forever | An open group exceeds its staging or destination's total capacity, and the CPU cannot reach a later boundary. | Impossible size and firing-width requests fault immediately; temporary occupancy alone causes admission backpressure. |
| Empty queue incorrectly means failure | CPU must wait for measurement before preparing a future point. | TCU time continues while empty; cumulative due cycles remain fixed. Late admission and manifest corruption are separate faults. |
| Initial empty origin blocks future work after START | The first positive wait tries to admit already-expired cycle zero. | The empty initial origin is implicit; positive advance can skip it. Actual cycle-zero actions still require pre-start admission. |
| Edge equality and same-edge queue credit are undefined | Different process order admits and fires a group on the same edge. | Strictly later crossing visibility, old occupancy, and no admission-edge firing; start edge has deterministic cycle zero. |
| Device distribution is on the wrong side of eQASM's queues | Output mapping and conflict resolution occur after a label is already triggered. | Producer-side device distribution and immutable action resolution precede admission; post-trigger logic performs bounded condition and launch checks. |
| Speculative CPU operations can leak irreversible effects | A younger command reaches the TCU before an older branch or fault resolves. | Only an authorized oldest non-speculative instruction can publish a side effect. |
| Physical resources lack one calendar owner | A currently idle channel already has a future overlapping reservation. | DeviceRuntime owns half-open physical intervals; a full batch is preflighted before mutation. |
| Backend calls can double-evolve shared state | Two ports independently advance overlapping pulses to their end times. | One state service batches boundaries and evolves the joint prior drive set once per interval. |
| Measurement identity and readiness are ambiguous | A second trigger allocates another token; result appears before discriminator arm; a stale completion overwrites a reused slot. | Allocate a token at acquisition acceptance, reserve both return paths, pair discriminator actions, and use explicit generation and epoch checks. |
| Fast feedback incorrectly depends on CPU visibility | A later outstanding measurement prevents use of an already completed result for a local predicate. | Discriminator completion feeds independent CPU and deterministic-domain paths. Predicates use a specified prior snapshot. |
| Reset and program halt can leave ghost or truncated work | Old delayed output fires after reset, or CPU END discards an admitted pulse. | Session reset invalidates callback epochs; END closes production and drains hardware work and result delivery. Controller-only reset is a separate future contract. |

## Evidence and attribution

- QuMA, Section 5.2: timing queue, timing labels, event queues and deterministic triggering. Section 5.3 also describes a post-trigger micro-operation sequencer; pre-expansion is a different modeling choice.
- eQASM, Sections 3.1 and 4.3: zero intervals identify the same point; operation combination and device distribution precede event queues; fast flags are independent of CPU result validity. Sections 3.6 and 4.3 describe pending-measurement validity and FMR.
- Distributed-HISQ, Sections 3.2 and 4: deterministic TCU time can pause while absolute simulated time continues. This does not justify using host runtime as a clock.
- SystemC channel and kernel behavior: deferred signal updates and delta notifications do not themselves implement a receiver-clock latency contract.

The paper sources and inspected local text locations are linked in [the module architecture](../module-architecture.md). Producer sealing, atomic admission, strict crossing rules, tagged consuming result reads, empty-stream behavior and device batching are explicit qsbit-sim baseline proposals, not claims about undocumented behavior in those papers.

## Validation and remaining decisions

Documentation checks cover local file links, Markdown structure and table widths, English repository content, and whitespace. The worked crossing and timer examples were independently recomputed. The [verification plan](../engineering-and-testing.md) now contains counterexample-based regression requirements for implementation. There is no simulator implementation or CTest suite in this repository yet; those behavioral tests have not been run.

Final machine encodings, CPU pipeline parameters, numerical capacities and crossing latencies, producer and admission bandwidth, and device adapter selections remain open. The baseline protocol has concrete ordering and completion rules. A different profile must document its changed rule and pass the relevant regression cases. External CACTUS validation must compare declared corresponding boundaries; producer acceptance and TCU group admission are distinct events and cannot be substituted for each other.
