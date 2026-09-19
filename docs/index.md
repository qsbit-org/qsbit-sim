# qsbit-sim documentation

qsbit-sim simulates an RV32I quantum controller. It executes a program, schedules
quantum operations at specified times, and returns measurement results to the
program. You can use it to study instruction timing, control queues and
measurement feedback with a scripted or numerical quantum backend.

The simulator uses C++20 and SystemC. Its timing control unit follows QuMA's
separation of command preparation from timed output. The CPU pipeline, instruction
adapter and quantum backend have separate interfaces.

## Start here

[Run your first simulation](quickstart.md) to build the controller, execute a
measurement-feedback program and inspect its result. The default scripted backend
requires no Python quantum packages.

To understand the model, read [Simulation time and execution](simulation-model.md)
and the [glossary](glossary.md), then the [architecture overview](high-level-design.md),
then [follow an execution](execution.md). The [architecture diagram](architecture.md)
links to each module's inputs, outputs, state and behavior.

| You want to… | Read |
| --- | --- |
| Install tools or change build options | [Prerequisites](prerequisites.md) and [building](building.md) |
| Use Aer, pulses or a custom backend | [Quantum backends](backends.md) |
| Write a run configuration or interpret a trace | [Program and file formats](interfaces.md) |
| Check a timing or ordering rule | [Control protocol](module-architecture.md) |
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
decisions/0002-reference-comparison-scope
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
