# C++ API reference

Use [interface contracts](cpp-interfaces.md) to understand the main adapter
boundaries, or select a header below to find its source. The namespace reference
contains the extracted declarations, including private state named by the
[module reference](modules/README.md).

(backend_8hpp)=
## backend.hpp

Quantum backend operations and the scripted implementation. [Open header](../include/qsbit/backend.hpp).

(control_8hpp)=
## control.hpp

Profiles, action mappings, group records and measurement tokens. [Open header](../include/qsbit/control.hpp).

(cpu_8hpp)=
## cpu.hpp

Replaceable CPU interface and the three-stage implementation. [Open header](../include/qsbit/cpu.hpp).

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

CPU result slots and TCU measurement history. [Open header](../include/qsbit/feedback.hpp).

(image_8hpp)=
## image.hpp

ELF/raw loading, memory bytes and segment permissions. [Open header](../include/qsbit/image.hpp).

(isa_8hpp)=
## isa.hpp

Decoded RV32I instructions and architectural effects. [Open header](../include/qsbit/isa.hpp).

(mailbox_8hpp)=
## mailbox.hpp

Bounded messages with explicit receiver eligibility ticks. [Open header](../include/qsbit/mailbox.hpp).

(memory_8hpp)=
## memory.hpp

Fetch/data ports and timed memory service. [Open header](../include/qsbit/memory.hpp).

(producer_8hpp)=
## producer.hpp

Quantum instruction adaptation, group preparation and crossings. [Open header](../include/qsbit/producer.hpp).

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
