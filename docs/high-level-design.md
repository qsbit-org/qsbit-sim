# Architecture overview

qsbit-sim executes a classical program that schedules quantum operations.
The CPU prepares port and codeword events, the timing control unit (TCU)
queues them, and the timing controller triggers them at the requested
time points. Measurement results return to the CPU and to conditional
execution in the TCU.

## Reserve phase and trigger phase

During the **reserve phase**, the CPU prepares events for a time point and
enqueues them. The timing queue records when the events are due; each
output port has an event queue. A timing label associates each time point
with its events.

During the **trigger phase**, the timing controller selects the due time
point and triggers its events. The TCU timer runs independently of the
CPU, so a CPU stall does not delay events already queued.

This separation follows [eQASM, Section 3.1](https://arxiv.org/abs/1808.02449).
The timing queue, event queues and timing labels follow
[QuMA, Section 5.2](https://arxiv.org/abs/1708.07677).

## Ports, codewords and time points

A port and codeword select one or more operations from the simulation profile.
Each operation specifies its output port, qubit targets, duration and output
delay. The program supplies the time point, expressed as a TCU cycle.

For example, two instructions can prepare events for cycle 8. Their time
point and event entries enter the queues together, before cycle 8. The TCU
triggers both at that cycle; each output starts after its configured delay.

Ports, codewords and time points are also the control abstraction used by
[HISQ](https://arxiv.org/html/2509.04798v1#S3.SS1.SSS2).

## Measurement and feedback

The backend samples a measurement at acquisition end. After the discriminator
delay, the result travels independently to the CPU and, when enabled, the TCU.

For classical feedback, QREAD returns the bit to a CPU register. The program
can then branch and schedule another operation. For fast conditional
execution, QAPPEND_IF attaches a measurement reference and expected bit to an
event. The TCU tests that condition when the event is due.

Conditions select an individual measurement. eQASM instead provides per-qubit
measurement registers and derived execution flags; those register semantics
are not implemented here.

## Implemented instruction profile

The executable accepts RV32I and seven custom-0 control encodings:

| Instruction | Purpose |
| --- | --- |
| QAPPEND | Prepare port and codeword events at the current time point. |
| QADVANCE | Enqueue pending events, then advance the time point. |
| QFLUSH | Enqueue pending events without advancing. |
| QREAD | Enqueue pending events and read a measurement result. |
| QEND | Enqueue pending events and end the program. |
| QAPPEND_IF | Prepare events conditioned on a measurement result. |
| QSYNC | Raise `UnsupportedSynchronization`. |

The [instruction reference](interfaces.md#quantum-instruction-encoding)
specifies operands, encodings and completion rules. These encodings are not
compatible with eQASM or HISQ. Distributed communication and synchronization
are not implemented.

## Simulation implementation

`TimingControl` prepares events. `TcuCycleModel` owns the timing queue,
per-port event queues and timer. `ControlElectronics` schedules output,
acquisition and result readiness, and calls the selected quantum backend.

`Simulator` runs these components with SystemC. See the
[controller diagram](architecture.md), [implementation map](implementation.md#implementation-map)
and [timing reference](module-architecture.md) for their connections and
execution order.
