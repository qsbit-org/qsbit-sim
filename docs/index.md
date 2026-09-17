# Understand the machine. Follow its time.

**qsbit-sim** models an RV32I quantum controller with SystemC scheduling,
QuMA-style timing queues, physical output channels and replaceable quantum backends.

Start with the [interactive architecture](architecture.md). Click a module to inspect
its objects, inputs, outputs and state transitions. Then [play a real execution](execution.md)
to follow instructions through admission, label firing, device output and measurement feedback.

| Explore | What you will find |
| --- | --- |
| [Architecture](architecture.md) | Clickable modules grouped by owner and clock domain. |
| [Execution player](execution.md) | Scripted Bell and feedback programs with exact recorded timestamps. |
| [Build and run](building.md) | Required tools, CMake options and optional backends. |
| [Module contracts](modules/README.md) | Local behavior, state ownership and executable verification. |
| [C++ API](api.md) | Classes, records and members extracted from the current headers. |
| [Glossary](glossary.md) | Cursor, group, admission, firing, crossing and simulation time. |

```{toctree}
:maxdepth: 2
:caption: Explore

architecture
execution
modules/README
```

```{toctree}
:maxdepth: 1
:caption: Use the simulator

prerequisites
building
backends
interfaces
glossary
```

```{toctree}
:maxdepth: 1
:caption: Develop and verify

high-level-design
module-architecture
implementation
cpp-interfaces
api
engineering-and-testing
website
decisions/0001-initial-implementation
decisions/0002-reference-comparison-scope
```
