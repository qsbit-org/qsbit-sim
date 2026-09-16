# Simulator Module Contracts

Each page describes one **logical module** in the Phase 1 architecture. A logical module is not automatically a separate `sc_module` or `SC_METHOD`. The CPU domain, TCU domain and DeviceRuntime are the principal clocked or timed owners; pure libraries and queue substates run inside those owners. Adding a SystemC process or a clock edge between two logical boxes would change the timing contract.

The [high-level design](../high-level-design.md) states project goals. Start with the [glossary](../glossary.md) if producer cursor, open group, admission, firing, or SystemC scheduling is unfamiliar. The [architecture overview](../module-architecture.md) defines cross-module protocol, especially producer sealing, strict receiver-edge visibility, TCU edge order, device batching and END drain. These pages own each module's local behavior and implementation contract; the selected values and encodings are fixed in [ADR 0001](../decisions/0001-initial-implementation.md). Every module page links to its implementation and focused tests. The [verification plan](../engineering-and-testing.md) gives regression scenarios.

| Module | Implementation boundary |
| --- | --- |
| [01. Platform Configuration and Clock Adapter](platform-and-clock-adapter.md) | Platform setup owns the immutable timing profile; a session coordinator owns reset requests. |
| [02. ELF Loader and Program Image](elf-loader-and-program-image.md) | Pure C++ startup component. |
| [03. ISA Decoder and Semantics](isa-decoder-and-semantics.md) | Pure C++ library called by the CPU owner. |
| [04. CPU Cycle Model](cpu-cycle-model.md) | One CPU-domain owner, normally wrapped by a clock-sensitive `SC_METHOD`; its pipeline is a replaceable pure C++ cycle model. |
| [05. Memory and Response Model](memory-and-response-model.md) | Clocked memory owner for runtime requests. |
| [06. Quantum Instruction Adapter](quantum-instruction-adapter.md) | Pure mapping called from the authorized CPU commit path. |
| [07. Operation Lowerer and Device Distributor](operation-lowerer-and-device-distributor.md) | Pure producer-side group resolver. |
| [08. Timeline Reservation Manager](timeline-reservation-manager.md) | Logical substate of the CPU-domain owner, not a second independently clocked process. |
| [09. Command Crossing and Atomic Admission](command-crossing-and-admission.md) | Crossing mailbox plus admission logic in the TCU-domain owner. |
| [10. Timing Queue](timing-queue.md) | Bounded FIFO substate of one TCU cycle model. |
| [11. Per-Port Event Queues](per-port-event-queues.md) | Bounded queue bank owned by the same TCU cycle model as the timing queue and broadcaster. |
| [12. TCU Timer and Label Broadcaster](tcu-timer-and-label-broadcaster.md) | One TCU rising-edge process owns deterministic time and invokes queue matching and launch preflight as logical substages. |
| [13. Condition Gate and Launch Preflight](condition-gate-and-launch-preflight.md) | Pure substage within the TCU edge transition. |
| [14. Port Codeword and Waveform Map](port-codeword-and-waveform-map.md) | Pure immutable lookup used during APPEND and group sealing, before TCU admission. |
| [15. Output Channels and Resource Calendar](output-channels-and-resource-calendar.md) | DeviceRuntime is the sole owner of physical channel occupancy and future interval reservations. |
| [16. Quantum-State Service and Backends](quantum-state-service-and-backends.md) | One chronological service is the sole caller of a replaceable quantum-state backend for a shared state. |
| [17. Acquisition and Discrimination](acquisition-and-discrimination.md) | DeviceRuntime-owned readout protocol substate and scheduled result-ready events. |
| [18. Measurement Scoreboard and CPU Feedback](measurement-scoreboard-and-cpu-feedback.md) | Token and slot state belongs to the CPU-domain producer and scoreboard; a separate committed mailbox handles CPU crossing. |
| [19. Fast-Condition History](fast-condition-history.md) | Optional TCU-domain history plus its own result crossing; it does not share CPU result visibility. |
| [20. Trace Recorder and Stop Controller](trace-recorder-and-stop-controller.md) | Two separate logical responsibilities documented together: trace is observation-only; the stop controller owns completion and fatal termination. |
| [21. Future Synchronization Adapter](future-synchronization-adapter.md) | Extension boundary only. |

The standalone CACTUS comparison project remains outside this repository. It consumes the generic public trace and does not add simulator module code.
