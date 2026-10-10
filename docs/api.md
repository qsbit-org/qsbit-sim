# C++ API reference

The headers below define internal C++ interfaces and are not installed. See
[C++ interfaces](cpp-interfaces.md) for adapter requirements and
[components](modules/README.md) for behavior and tests.

(backend_8hpp)=
## backend.hpp

Quantum backend operations and the mock implementation. [Open header](../include/qsbit/backend.hpp).

(profile_8hpp)=
## profile.hpp

Full simulation profile, validation and configuration fingerprint.
[Open header](../include/qsbit/profile.hpp).

(control__protocol_8hpp)=
## control_protocol.hpp

Timing-point requests, underflow policies and scheduled events. [Open header](../include/qsbit/control_protocol.hpp).

(cpu_8hpp)=
## cpu.hpp

Replaceable CPU interface with memory and control ports. [Open header](../include/qsbit/cpu.hpp).

(core_8hpp)=
## core.hpp

Per-controller CPU, memory, timing control, synchronization and measurement state. [Open header](../include/qsbit/core.hpp).

(sync_8hpp)=
## sync.hpp

Countdowns and bounded fixed-delay connections for BISP, the booking-based
synchronization protocol from Distributed-HISQ. [Open header](../include/qsbit/sync.hpp).

(rv32_8hpp)=
## cpu/rv32.hpp

Three-stage RV32I CPU with scalar qsbit control instructions. [Open header](../include/qsbit/cpu/rv32.hpp).

(vliw_8hpp)=
## cpu/vliw.hpp

Three-stage CPU with scalar instructions and dual-codeword bundles. [Open header](../include/qsbit/cpu/vliw.hpp).

(cpu_2trace_8hpp)=
## cpu/trace.hpp

End-of-edge CPU pipeline snapshots. [Open header](../include/qsbit/cpu/trace.hpp).

(pipeline_8hpp)=
## cpu/pipeline.hpp

Shared in-order pipeline state and transitions. [Open header](../include/qsbit/cpu/pipeline.hpp).

(defaults_8hpp)=
## defaults.hpp

Construction of the default timing and device profile. [Open header](../include/qsbit/defaults.hpp).

(decoder_8hpp)=
## decoder.hpp

Decoder MMIO registers, bounded transport and timed processing. [Open header](../include/qsbit/decoder.hpp).

(device_8hpp)=
## device.hpp

Physical intervals, resource reservations and device execution. [Open header](../include/qsbit/device.hpp).

(resources_8hpp)=
## device/resources.hpp

Output-interval and resource-conflict checks. [Open header](../include/qsbit/device/resources.hpp).

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

Quantum instruction adaptation, time point preparation, mailbox requests and
replies, and feedback communication. [Open header](../include/qsbit/timing_control.hpp).

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

(event_8hpp)=
## event.hpp

Device event specifications, codeword mappings and paired-gate definitions.
[Open header](../include/qsbit/event.hpp).

(measurement_8hpp)=
## measurement.hpp

Measurement identities, completion bits and execution flags.
[Open header](../include/qsbit/measurement.hpp).

(control__command_8hpp)=
## control_command.hpp

Named payloads for codeword, wait, measurement-read, halt and synchronization commands.
[Open header](../include/qsbit/control_command.hpp).

(timing__config_8hpp)=
## timing_config.hpp

Transport, timing-control and TCU configuration values.
[Open header](../include/qsbit/timing_config.hpp).

(gates_8hpp)=
## device/gates.hpp

Indexed paired-gate definitions and gate-output resolution.
[Open header](../include/qsbit/device/gates.hpp).

(readouts_8hpp)=
## device/readouts.hpp

Acquisition pairing, readiness calculation and sampled-result lifetime.
[Open header](../include/qsbit/device/readouts.hpp).

(mmio_8hpp)=
## decoder/mmio.hpp

Decoder register storage and typed MMIO commands.
[Open header](../include/qsbit/decoder/mmio.hpp).

## Namespace reference

```{doxygennamespace} qsbit
:project: qsbit
:members:
:undoc-members:
:private-members:
```
