# Architecture overview

qsbit-sim executes RV32I programs with HISQ codeword and timing instructions
and eQASM measurement-register reads. The CPU prepares events, the timing
control unit (TCU) queues them, and the timing controller triggers them at
the requested time points.

## Reserve phase and trigger phase

During the **reserve phase**, `cw` selects a port and codeword at the current
time point. `wait` advances that point by a specified number of TCU cycles.
The timing queue stores time points, and each output port has an event queue.

During the **trigger phase**, the timing controller releases the events for
the due time point. The TCU timer runs independently of the CPU, so a CPU
stall does not delay events already queued.

See [QuMA, Section 5.2](https://arxiv.org/abs/1708.07677),
[eQASM, Section 3.1](https://arxiv.org/abs/1808.02449) and
[HISQ, Section 3.1](https://arxiv.org/html/2509.04798v1#S3.SS1).

## Codeword-triggered control

Each port and codeword selects configured operations with output ports,
qubit targets, durations and delays. A codeword can trigger an ideal gate,
a pulse, an acquisition or a discriminator arm.

Two `cw` instructions at cycle 8 schedule their events for that cycle.
A later positive `wait` enqueues the time point and its events. Each output
starts at cycle 8 plus its configured delay.

## Measurement and feedback

Each qubit has a measurement result register. Accepting a measurement
increments its pending count. Receiving the result updates its bit and
decrements the count. `FMR` waits for the count to reach zero, then copies
the bit to a CPU register. Repeated reads return the same bit until another
measurement completes.

Execution flags control single-qubit operations at the trigger edge.
The codeword mapping selects one of four flags:

| Flag | Execute when |
| --- | --- |
| `always` | Unconditionally. |
| `last_one` | The latest completed measurement returned one. |
| `last_zero` | The latest completed measurement returned zero. |
| `equal` | The latest two completed measurements returned the same bit. |

The flags use results received by the TCU. They update independently of
CPU register reads and pending measurements. See
[eQASM, Sections 3.5 and 3.6](https://arxiv.org/html/1808.02449#S3.S5).

## Simulation implementation

`TimingControl` prepares events. `TcuCycleModel` owns the timing queue,
per-port event queues, timer and execution flags. `ControlElectronics`
schedules outputs, acquisition and result delivery. `Simulator` runs
these components with SystemC.

The [instruction reference](interfaces.md#quantum-instruction-encoding)
defines the simulator's machine-code format. Cross-controller communication
and synchronization are unsupported.

See the [controller diagram](architecture.md),
[implementation map](implementation.md#implementation-map) and
[timing reference](module-architecture.md).
