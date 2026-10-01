# Architecture overview

qsbit-sim models a programmable quantum controller: a classical processor
prepares operations, timing and event queues retain them, and a timing controller
triggers their output at the specified time points. Measurements return results
for classical feedback or fast conditional execution.

## Reserve phase and trigger phase

During the **reserve phase**, the processor decodes instructions and enqueues
timing points and their associated operation events. A timing point specifies
when events should occur. Each output port has an event queue.

During the **trigger phase**, the timing controller reaches a queued time point
and triggers the corresponding events. The timing queue decouples instruction
execution from precise output timing. A CPU stall does not pause the running
TCU timer in the current model.

These phase names follow [eQASM, Section 3.1](https://arxiv.org/abs/1808.02449).
[QuMA, Section 5.2](https://arxiv.org/abs/1708.07677) defines the timing queue,
event queues and timing controller. Its timing labels associate events with
time points; a label is an identifier, not a timestamp.

## Ports, codewords and time points

A control instruction selects a port and a codeword. The configuration maps
them to output events, including pulse generation, acquisition and discrimination.
The program specifies the time point for each event. This follows the control
abstraction in [HISQ, Sections 2.2 and 3.1.2](https://arxiv.org/html/2509.04798v1#S3.SS1.SSS2).

For example, two codeword instructions can prepare events for TCU cycle 8.
The simulator collects those events before enqueueing their timing point and
event entries together. At cycle 8 the timing controller triggers both events.
Configured output delays determine when the corresponding control outputs start.
The enqueue request must reach the queues before the trigger edge.

## Measurement and feedback

The measurement path includes acquisition and discrimination. The model samples
at acquisition end and delivers the resulting bit after discriminator and
communication delays.

Classical feedback reads a measurement result into a CPU register and uses
ordinary branches to choose subsequent operations. Fast conditional execution
tests a measurement result at the event trigger, avoiding a CPU branch. Both
paths preserve the existing timing points.

[eQASM, Sections 2.3.7–2.3.8 and 3.5–3.6](https://arxiv.org/abs/1808.02449) defines
measurement result registers and execution flags. qsbit-sim currently selects
an individual measurement by reference; it does not implement eQASM's per-qubit
register and derived execution-flag semantics. The result references and retained
measurements are specified in the [simulator interfaces](interfaces.md#quantum-instruction-encoding).

## Implemented instruction profile

The current executable accepts RV32I and its existing custom-0 control encodings.
QAPPEND supplies the port and codeword; QADVANCE advances the requested time
point. Their roles correspond to HISQ's `cw` and `wait`. Their encodings and
completion rules remain those of the [existing profile](interfaces.md#quantum-instruction-encoding).

QREAD reads a local measurement reference. QFLUSH and QEND submit pending events
and control program completion. QAPPEND_IF attaches a measurement condition.
These instructions are simulator interface extensions, not HISQ instructions.
QSYNC remains unsupported; HISQ `send`, `recv`, synchronization and distributed
control are not implemented.

## Simulation implementation

The [controller diagram](architecture.md) shows the control path. The
[implementation map](implementation.md#implementation-map) separately shows
C++ objects, communication, numerical adapters and SystemC scheduling.

`TimingControl` implements reserve-phase preparation. `TcuCycleModel` owns
the timing and event queues. `ControlElectronics` schedules control output,
acquisition and discriminator delay. A quantum backend supplies shared quantum
state and measurement calculations; it is a simulator interface for the quantum
device. The resource conflict checker is also a simulation facility.

`Simulator` schedules these objects with SystemC. Message arrival times,
same-tick processing, reset epochs and result-slot reuse are defined in the
[simulation timing contract](module-architecture.md).
