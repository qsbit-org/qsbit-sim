# Architecture overview

qsbit-sim executes RV32I programs with qsbit control instructions inspired by
HISQ and eQASM. The CPU prepares control events. The timing control unit (TCU)
queues and triggers them at scheduled time points. qsbit-sim uses its own
[instruction encodings](interfaces.md#quantum-instruction-encoding), not HISQ or
eQASM binaries.

## Reserve phase and trigger phase

During the **reserve phase**, `cw` selects a source port and codeword at the current
time point. `wait` advances that point by a specified number of TCU cycles.
The timing queue stores time points, and each output port has an event queue.

During the **trigger phase**, the timing controller releases the events for
the due time point. The TCU timer runs independently of the CPU, so a CPU
stall does not delay events already queued. After `wait 0` triggers, an empty
queue pauses the logical timer while physical time continues. See the
[pause and deadline rules](module-architecture.md#start-deadlines-and-empty-queues).

See [QuMA, Section 5.2](https://arxiv.org/abs/1708.07677),
[eQASM, Section 3.1](https://arxiv.org/abs/1808.02449) and
[HISQ, Section 3.1](https://arxiv.org/html/2509.04798v1#S3.SS1).

## Codeword-triggered control

Each source-port and codeword pair selects configured operations with output ports,
qubit targets, durations and delays. A codeword can trigger an ideal gate,
a pulse, an acquisition, a discriminator arm or a paired two-qubit gate output
(`gate_output`).

Two `cw` instructions at cycle 8 schedule their events for that cycle.
A later positive `wait` enqueues the time point and its events. The time point
triggers at logical cycle 8; each physical output starts at that trigger tick
plus its configured delay in nanoseconds.

## Measurement and feedback

Each qubit has a measurement result register. Accepting a measurement
increments its pending count. Receiving the result updates its bit and
decrements the count. `fmr` waits for the count to reach zero, then copies
the bit to a CPU register. Repeated reads return the same bit until another
measurement completes.

Each mapped single-qubit gate or pulse can specify one of four execution
conditions. The TCU evaluates measurement-dependent conditions using execution
flags committed before the trigger edge:

| Condition | Execute when |
| --- | --- |
| `always` | Unconditionally. |
| `last_one` | The latest completed measurement returned one. |
| `last_zero` | The latest completed measurement returned zero. |
| `equal` | The latest two completed measurements returned the same bit. |

The flags use results received by the TCU. They update independently of
CPU register reads and pending measurements. See
[eQASM, Sections 3.5 and 3.6](https://arxiv.org/html/1808.02449#S3.S5).

## Multiple controllers

Controller cores share one quantum device. Neighbor synchronization uses
BISP, the booking-based synchronization protocol from Distributed-HISQ.
Regional synchronization and inter-controller `send` and `recv` messaging are unsupported. See
[distributed simulation](distributed-simulation.md).

See the [controller diagram](architecture.md),
[implementation map](implementation.md#implementation-map) and
[timing reference](module-architecture.md).
