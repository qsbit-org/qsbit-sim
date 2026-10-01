# Glossary

## Controller architecture

These terms describe the control path. The
[architecture overview](high-level-design.md) identifies the supported subset.

### Reserve phase

Instruction processing that prepares timing points and operation events for
the queues. eQASM defines reserve and trigger phases in
[Section 3.1](https://arxiv.org/abs/1808.02449).

### Trigger phase

Release of queued events at their specified timing points.

### TCU

The timing control unit. It contains the timing queue, event queues and timing
controller described in [QuMA, Section 5.2](https://arxiv.org/abs/1708.07677).

### Time point

A specified time for one or more operation events. The current time point being
prepared is independent of the running TCU timer. The simulator expresses it
as a logical TCU cycle.

### Timing label

The identifier associating a time point with its event-queue entries. QuMA
explicitly uses timing labels and label broadcast in Section 5.2. A timing
label is not a timestamp.

### Timing queue

The first-in, first-out queue of timing points. The simulator stores intervals
and calculates cumulative due cycles.

### Per-port event queue

The first-in, first-out queue of events for one output port. HISQ describes
per-port event queues in [Section 3.2](https://arxiv.org/html/2509.04798v1#S3.SS2).

### Port and codeword

A port identifies a control destination; a codeword selects its configured
operation. HISQ uses ports, codewords and time points to specify control.
In the simulation profile, the instruction's port need not equal a qubit index
or the output port selected by the mapping.

### Enqueue

Insertion of a timing point and its associated events into the timing and
event queues. qsbit-sim checks capacity and inserts all entries together.

### Event trigger

Release of the events associated with a due timing label. Configured output
delays determine their subsequent start times.

### Readout

The path from acquisition through discrimination to measurement-result delivery.

### Acquisition

A timed measurement interval. The current simulation samples and collapses
quantum state at its end.

### Discrimination

Conversion of a readout signal to a measurement bit. The simulator models the
delay; its backend supplies the bit without classifying a raw waveform.

### Classical feedback

Reading a measurement result into the CPU and using program control flow to
select later operations.

### Fast conditional execution

Selecting an event based on a measurement-derived condition without a CPU
branch. eQASM defines execution flags for this purpose in Sections 2.3.8 and
3.5. The current simulator instead tests an exact measurement reference and
expected bit; these semantics are not interchangeable.

## Simulation time and scheduling

### SystemC

A C++ discrete-event simulation library. Its kernel runs ready processes and
advances simulation time to the next scheduled event.

### Simulation time

Time in the modeled system. It continues across session resets.

### Host time

Elapsed execution time on the computer running the simulator. A backend's host
computation time does not change modeled instruction or device latencies.

### Tick

One nanosecond of simulation time. The C++ alias `Tick` also stores some cycle
counts; consult the [field units](cpp-interfaces.md#time-and-cycle-units).

### Clock period and phase

Period is the interval between rising edges. Phase is the first rising-edge
offset from global time zero. Edges occur at `phase + k * period`.

### TCU logical cycle

Cycle n is due at `epoch_start + n * tcu.period`. The initial origin is
`profile.start`. After reset, it is the first TCU edge at or after
`reset_tick + profile.start`.

### Arrival time

The receiver edge at which a published message first becomes available after
its configured communication delay. A full destination can delay consumption.
The C++ mailbox field retains the compatibility name `eligible`.

### Mailbox

Bounded simulator message storage containing a payload, publication tick,
arrival tick and epoch. A SystemC notification wakes a consumer; it does not
carry the payload.

### Elaboration

Construction of SystemC modules, processes and connections before simulation.

### Initialization

The initial kernel phase. `dont_initialize()` suppresses automatic invocation;
an event at time zero can still trigger the process.

### Sensitivity

The events that make a SystemC process runnable. The simulator uses clock-edge,
timed-wakeup and barrier events.

### Delta cycle

A scheduling round that does not advance simulation time or hardware cycles.

### Device barrier

An implementation method that processes device work after all clocked
transitions due at that tick have finished. It does not represent a hardware
pipeline stage.

### Physical boundary

An implementation timestamp for output starts, ends, measurement samples or
result readiness. It can occur between clock edges.

## Programs and CPU execution

### RV32I

The base 32-bit RISC-V integer instruction set. The simulator adds its
[custom-0 control profile](interfaces.md#quantum-instruction-encoding).

### ISA and microarchitecture

An ISA specifies instruction encodings and architectural effects.
A microarchitecture determines how an implementation executes those
instructions, including stages, hazards and timing.

### CPU pipeline

The default CPU advances fetch, decode and execute stages on CPU rising edges.
Each stored instruction advances at most one stage per edge. Retirement
commits its architectural effects.

### Stall

An unfinished instruction retains its state and retries on a later CPU edge.
A pending load or measurement read can stall execution while the TCU continues.

### Speculative instruction

Younger work fetched before older control flow or faults are resolved.
Only the oldest instruction can publish stores or control effects.

### Program image

Executable bytes, permissions and entry address loaded from ELF or raw machine
code. Memory requests and responses have separately modeled delays.

## Simulator records and checks

The following are implementation data and services. Their C++ declarations are
in the [interface reference](cpp-interfaces.md).

### Timing and event records

`TimingEvents` carries one timing point and its associated events to the TCU.
`EnqueueReply` acknowledges queue insertion. `TriggeredEvents` carries the
events selected at one trigger edge. These records do not add hardware stages.

### Measurement reference

`MeasurementReference` identifies an individual measurement using its epoch,
measurement ID, result slot, slot generation, handle and target.
The 32-bit handle returned to the program is not a qubit index.
QREAD consumes the result slot; a previously prepared condition retains its
reference. Slot generations prevent stale handles from becoming valid on reuse.

### Measurement storage

`MeasurementResults` stores pending and CPU-visible results.
`ConditionalResults` retains bounded measurement results for TCU conditions.
They implement the current profile, not eQASM's architectural register files.

### Resource

A configured identifier used by the simulation's output-conflict checks.
Intervals sharing a resource conflict if either requires exclusive use.
These checks also prohibit overlapping events on the same output port.

### Event specification

`EventSpec` contains configured output parameters. `ScheduledEvent` adds
start and end timestamps. Output occupies the half-open interval
`[start, end)`; adjacent intervals may share an endpoint.

### Quantum backend

The numerical or scripted implementation of the quantum device.
`IQuantumBackend` defines validation, evolution, gate application and measurement.
It is a simulator API, not a controller hardware module.

### Ideal gate and pulse drive

An ideal gate changes state at its start. A pulse drive contributes throughout
its interval; overlapping permitted drives evolve jointly against shared state.

## Configuration and completion

### Run configuration

A JSON file selecting the program, backend, profile and output files.
Paths are relative to the file's directory.

### Simulation profile

Clock periods, delays, capacities and codeword mappings fixed for a run.
A Conan build profile instead selects compiler dependencies.

### Drain

Completion of pending queue entries, device events, memory transactions and
enabled result deliveries after QEND. Unread CPU-visible results may remain.

### Session reset

A new simulation epoch with controller and quantum state reset.
Committed memory and the profile remain unchanged. Simulation time continues;
measurement-slot generations remain valid for detecting stale references.

### Trace record

A logged observation containing a timestamp and event-specific fields.
Schema-1 names remain stable for existing consumers; see
[trace formats](interfaces.md#jsonl-trace).

### Watchdog

A global simulation deadline. Reaching it without draining reports a failure.
