# qsbit-sim documentation

qsbit-sim executes RV32I programs with quantum-control instructions. It schedules
quantum operations and returns measurement results to the program. Use it to
study CPU timing, control queues and feedback with a mock or numerical backend.

## Start here

[Run your first simulation](quickstart.md) to build the controller, execute a
measurement-feedback program and inspect its result. The default mock backend
requires no Python quantum packages.

To understand the control path, read the [architecture overview](high-level-design.md)
and [follow an execution](execution.md). The [component reference](modules/README.md)
links each component to its source and tests.

| You want to… | Read |
| --- | --- |
| Install tools or change build options | [Prerequisites](prerequisites.md) and [building](building.md) |
| Configure Aer, Stim, pulses or a custom backend | [Quantum backends](backends.md) |
| Write a run configuration or interpret a trace | [Program and file formats](interfaces.md) |
| Check a timing or ordering rule | [Simulation timing contract](module-architecture.md) |
| Run multiple controllers with BISP | [Distributed simulation](distributed-simulation.md) |
| Find a class or replace the CPU model | [C++ interfaces](cpp-interfaces.md) and [API reference](api.md) |
| Run tests or edit the website | [Testing](engineering-and-testing.md) and [website development](website.md) |

```{toctree}
:maxdepth: 1
:caption: Get started

quickstart
execution
```

```{toctree}
:maxdepth: 1
:caption: Guides

prerequisites
building
backends
repeated-simulations
distributed-simulation
engineering-and-testing
website
```

```{toctree}
:maxdepth: 1
:caption: Understand the controller

simulation-model
high-level-design
architecture
decisions/0001-initial-implementation
```

```{toctree}
:maxdepth: 1
:caption: Reference

module-architecture
modules/README
implementation
interfaces
cpp-interfaces
api
glossary
```
