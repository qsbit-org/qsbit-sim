# qsbit-sim

A C++20 and SystemC simulation project for a programmable quantum control processor. The first design target combines an extensible RV32I classical execution engine with a QuMA-inspired timing control unit.

Phase 1 targets RV32I extension programs, cycle-level control timing, and exact boundary-event agreement with CACTUS for equivalent workloads. An MMIO-only demonstration is an earlier smoke test. TQEC integration is reserved for Phase 2. CACTUS comparison tooling lives in a separate disposable validation project and uses only the simulator's public interfaces.

The architecture is defined in [docs/high-level-design.md](docs/high-level-design.md), with module ownership and QuMA-style TCU behavior detailed in [docs/module-architecture.md](docs/module-architecture.md). Start with the [glossary](docs/glossary.md) for timing, SystemC, queue, and feedback terminology. The [module contract index](docs/modules/README.md) links to a behavior description and diagram for each logical module. Engineering and verification rules are in [docs/engineering-and-testing.md](docs/engineering-and-testing.md) and [AGENTS.md](AGENTS.md).

This repository currently contains design documents. It does not claim a working simulator yet.
