# Glossary

## Controller architecture

### Reserve phase

Instruction processing that prepares and enqueues time points and their events.

### Trigger phase

Release of queued events at their scheduled time points. See
[eQASM, Section 3.1](https://arxiv.org/abs/1808.02449).

### TCU

The timing control unit: timing queue, per-port event queues and timing
controller. See [QuMA, Section 5.2](https://arxiv.org/abs/1708.07677).

### Time point

A scheduled TCU cycle for zero or more events. The CPU prepares the current
time point independently of the running TCU timer.

### Timing queue

A FIFO of time points, each specifying an interval from the preceding point.

### Per-port event queue

A FIFO of events for one output port, ordered by time point.

### Port and codeword

The lookup key for configured operations. The instruction selects a source
port and codeword; the mapping specifies output ports and qubit targets.

### Enqueue

Insertion of a time point and all its events into the TCU queues together.

### Event trigger

Release of the events for a due time point. Each output starts after its
configured delay.

### Readout

Acquisition, discrimination and delivery of a measurement result.

### Acquisition

A measurement interval. The backend samples at its end.

### Discrimination

Conversion of a readout signal to a bit. qsbit-sim models the delay and
takes the bit from the backend.

### Classical feedback

Reading a measurement result into the CPU and using program control flow
to select subsequent operations.

### Fast conditional execution

Executing or canceling a single-qubit operation at its trigger edge according
to the target qubit's selected execution flag.

### Measurement result register

One bit per qubit, read by `FMR`. A pending count prevents reads until all
accepted measurements of that qubit complete.

### Execution flag

A per-qubit bit selecting unconditional execution, a latest result of one,
a latest result of zero, or equality of the latest two results.

## Simulation time and scheduling

### SystemC

The C++ discrete-event simulation library that schedules model execution.

### Simulation time

Time in the modeled system, measured in nanoseconds. It continues across resets.

### Host time

Elapsed execution time on the computer running the simulator.

### Tick

One nanosecond of simulation time. The C++ type `Tick` also stores some
cycle counts; see [field units](cpp-interfaces.md#time-and-cycle-units).

### Clock period and phase

Period is the interval between rising edges. Phase is the first rising edge's
offset from time zero. Edges occur at `phase + k * period`.

### TCU logical cycle

Cycle n occurs at `epoch_start + n * tcu.period`. Initially, `epoch_start`
is `profile.start`; reset calculates a [new start](module-architecture.md#session-reset).

### Arrival time

The earliest receiver edge at which a published message can be consumed.
The mailbox stores it in `eligible`.

### Mailbox

Bounded storage that retains each message with its epoch, publication tick
and arrival tick until consumption.

### Elaboration

Construction of SystemC modules, processes and connections before simulation.

### Initialization

The initial SystemC kernel phase. `dont_initialize()` suppresses automatic
process invocation; an event at time zero can still trigger the process.

### Sensitivity

The events that make a SystemC process runnable.

### Delta cycle

A scheduling round that does not advance simulation time.

### Device barrier

`Simulator::barrier()`, which processes device events after all clocked
methods due at the same tick have finished.

### Physical boundary

A tick with scheduled output starts, ends, measurement samples or ready results.

## Programs and CPU execution

### RV32I

The base 32-bit RISC-V integer instruction set. qsbit-sim adds
[custom-0 control instructions](interfaces.md#quantum-instruction-encoding).

### ISA and microarchitecture

The ISA defines instruction encodings and effects. The microarchitecture
defines how they execute, including pipeline stages, stalls and timing.

### CPU pipeline

Fetch, decode and execute stages. Each instruction advances at most one stage
per CPU edge. Execute also commits its result.

### Stall

An unfinished instruction retains its state until a later CPU edge retries it.

### Speculative instruction

An instruction fetched before older control flow or faults are resolved.
Only the oldest instruction can issue stores or control operations.

### Program image

Executable bytes, segment permissions and entry address loaded from ELF or
raw machine code.

## Simulator records and checks

### Timing label

An identifier that associates a time point with its event-queue entries.

### Timing and event records

`TimingEvents` carries a time point and its events.
`EnqueueReply` acknowledges insertion.
`TriggeredEvents` carries the events selected at a trigger edge.

### Measurement reference

`MeasurementReference` identifies a pending delivery by epoch, measurement ID
and target qubit.

### Measurement storage

`MeasurementRegisters` stores per-qubit result bits and pending counts.
`ExecutionFlags` stores per-qubit flags for trigger-time checks.

### Resource

A configured ID used to detect conflicting output intervals. Overlapping
intervals sharing an ID conflict if either requires exclusive use.

### Event specification

`EventSpec` defines an operation's output parameters. `ScheduledEvent`
adds start and end ticks. The operation occupies `[start, end)`.

### Quantum backend

An implementation of `IQuantumBackend` that supplies quantum evolution and
measurement outcomes. The mock backend supplies fixed outcomes without a
quantum state.

### Ideal gate and pulse drive

An ideal gate changes state at its start. A pulse drive contributes to
evolution throughout its interval.

## Configuration and completion

### Run configuration

A JSON file selecting the program, backend, profile and outputs.
Paths resolve relative to the file's directory.

### Simulation profile

Clock periods, delays, capacities and codeword mappings fixed for a run.

### Drain

Completion of pending events, memory transactions and result deliveries
after the exit ECALL.

### Session reset

A new epoch with controller and backend state reset. Memory and the profile
are preserved.

### Trace record

A timestamped observation with event-specific fields. See
[trace formats](interfaces.md#jsonl-trace).

### Watchdog

The global simulation deadline. The run fails if it has not drained when
the deadline is checked.
