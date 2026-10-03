# C++ API reference

The headers below define the simulator's public interfaces. See
[C++ interfaces](cpp-interfaces.md) for adapter requirements and
[components](modules/README.md) for behavior and tests.

(backend_8hpp)=
## backend.hpp

Quantum backend operations and the mock implementation. [Open header](../include/qsbit/backend.hpp).

(control_8hpp)=
## control.hpp

Profiles, event mappings, time point records and measurement references. [Open header](../include/qsbit/control.hpp).

(cpu_8hpp)=
## cpu.hpp

Replaceable CPU interface with memory and control ports. [Open header](../include/qsbit/cpu.hpp).

(rv32_8hpp)=
## cpu/rv32.hpp

Three-stage RV32I CPU with scalar HISQ control instructions. [Open header](../include/qsbit/cpu/rv32.hpp).

(vliw_8hpp)=
## cpu/vliw.hpp

Three-stage CPU with scalar instructions and dual-codeword bundles. [Open header](../include/qsbit/cpu/vliw.hpp).

(defaults_8hpp)=
## defaults.hpp

Construction of the default timing and device profile. [Open header](../include/qsbit/defaults.hpp).

(device_8hpp)=
## device.hpp

Physical intervals, resource reservations and device execution. [Open header](../include/qsbit/device.hpp).

(error_8hpp)=
## error.hpp

Typed faults and their diagnostic names. [Open header](../include/qsbit/error.hpp).

(feedback_8hpp)=
## feedback.hpp

Per-qubit measurement result registers and execution flags. [Open header](../include/qsbit/feedback.hpp).

(image_8hpp)=
## image.hpp

ELF or raw loading, memory bytes and segment permissions. [Open header](../include/qsbit/image.hpp).

(isa_8hpp)=
## isa.hpp

Decoded RV32I instructions and architectural effects. [Open header](../include/qsbit/isa.hpp).

(mailbox_8hpp)=
## mailbox.hpp

Bounded messages with explicit arrival ticks. [Open header](../include/qsbit/mailbox.hpp).

(memory_8hpp)=
## memory.hpp

Fetch and data ports with timed memory service. [Open header](../include/qsbit/memory.hpp).

(timing__control_8hpp)=
## timing_control.hpp

Quantum instruction adaptation, time point preparation and crossings. [Open header](../include/qsbit/timing_control.hpp).

(python__backend_8hpp)=
## python_backend.hpp

Optional bridge to Python quantum adapters. [Open header](../include/qsbit/python_backend.hpp).

(simulator_8hpp)=
## simulator.hpp

SystemC scheduling, CPU construction, reset and completion. [Open header](../include/qsbit/simulator.hpp).

(tcu_8hpp)=
## tcu.hpp

Timing queues, event queues and the TCU edge transition. [Open header](../include/qsbit/tcu.hpp).

(time_8hpp)=
## time.hpp

Tick arithmetic and clock-edge calculations. [Open header](../include/qsbit/time.hpp).

(trace_8hpp)=
## trace.hpp

Trace records and JSONL serialization. [Open header](../include/qsbit/trace.hpp).

## Namespace reference

```{doxygennamespace} qsbit
:project: qsbit
:members:
:undoc-members:
:private-members:
```
