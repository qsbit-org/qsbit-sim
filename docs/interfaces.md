# Program, configuration and trace reference

The simulator accepts ELF or raw machine code and JSON configuration files.
Run files, serialized profiles and traces use schema version 1.

## Program input

ELF programs must be little-endian ELF32 RISC-V executables with valid,
nonoverlapping `PT_LOAD` segments and an aligned executable entry address.
Compressed-instruction flags are rejected. Loading enforces segment permissions
and zero-fills BSS.

Raw programs require `--raw-base` and a nonempty file whose size is a multiple
of four bytes. The simulator reads machine code, not assembly text.

Memory accesses are little-endian. Misaligned halfword, word and instruction
accesses raise typed faults. Unmapped bytes within configured RAM can hold data,
but instruction fetches require executable mappings.

The ISA is RV32I with codeword, timing and measurement-register instructions.
Registers and addresses are 32 bits. ECALL with `a7 = 93` and `a0 = 0`
ends the program. Other ECALLs and EBREAK raise distinct traps. FENCE is
supported; FENCE.I, privileged instructions and unselected extensions raise
`IllegalInstruction`.

### CPU selection

`rv32` is the default CPU model. `vliw` accepts the same instructions and adds
32-bit words containing two `cw` operations.

```sh
build-clang/qsbit-sim --program program.elf --cpu-model rv32
build-clang/qsbit-sim --program program.elf --cpu-model vliw
```

In a run file, set `"cpu_model": "vliw"`. Assemble paired operations with
`cw.bundle`; selecting the CPU model does not rewrite the program.
`rv32` rejects bundle instructions with `IllegalInstruction`.

## Quantum instruction encoding

Scalar control instructions use opcode `0x0b` in the RISC-V custom-0 space.

```text
31          25 24     20 19     15 14   12 11     7 6       0
    funct7       rs2       rs1    funct3     rd       opcode
```

| funct3 | Instruction | Fields |
| --- | --- | --- |
| 0 | `cw.r.r port, codeword` | `funct7 = 0`; rs1 and rs2 select GPRs; rd is zero. |
| 0 | `cw.i.r port, codeword` | `funct7 = 1`; rs1 is a 5-bit immediate; rs2 selects a GPR; rd is zero. |
| 0 | `cw.r.i port, codeword` | `funct7 = 2`; rs1 selects a GPR; rs2 is a 5-bit immediate; rd is zero. |
| 0 | `cw.i.i port, codeword` | `funct7 = 3`; rs1 and rs2 are 5-bit immediates; rd is zero. |
| 1 | `wait.r interval` | rs1 selects a GPR; funct7, rs2 and rd are zero. |
| 2 | `wait.i interval` | Bits 31–15 hold an unsigned 17-bit interval; rd is zero. |
| 3 | `FMR rd, target` | rs1 holds a 5-bit qubit index; funct7 and rs2 are zero. |
| 6 | `sync target` | Bits 31–15 hold an unsigned 17-bit controller address; rd is zero. |

funct3 values 4, 5 and 7 are reserved. Invalid fixed fields raise
`IllegalInstruction`. `sync` schedules a synchronization event with a connected
neighbor; an unconnected target raises `UnsupportedSynchronization`.
`send` and `recv` are unsupported.

### Dual-codeword encoding

`cw.bundle` uses opcode `0x2b` in the RISC-V custom-1 space and requires
the `vliw` CPU model.

| Word bits | Field |
| --- | --- |
| 31 | Zero; a set bit raises `IllegalInstruction`. |
| 30–19 | Operation 1. |
| 18–7 | Operation 0. |
| 6–0 | Opcode `0x2b`. |

Each operation occupies 12 bits:

| Operation bits | Field |
| --- | --- |
| 11 | One for an immediate codeword; zero for a register operand. |
| 10 | One for an immediate port; zero for a register operand. |
| 9–5 | Codeword immediate or register index. |
| 4–0 | Port immediate or register index. |

The GNU assembler macro accepts four operands and two optional modes:

```text
cw.bundle port0, codeword0, port1, codeword1, mode0=3, mode1=3
```

Port and codeword arguments are integers from 0 to 31. Register operands use
register numbers; their resolved values retain all 32 bits. Each mode defaults
to 3:

| Mode | Port operand | Codeword operand |
| --- | --- | --- |
| 0 | Register | Register |
| 1 | Immediate | Register |
| 2 | Register | Immediate |
| 3 | Immediate | Immediate |

```asm
cw.bundle 0, 1, 1, 2
li x5, 1
li x6, 2
wait.i 1
cw.bundle 0, 1, 5, 6, 3, 0
```

Both bundles prepare codeword 1 on port 0 and codeword 2 on port 1.
The two operations in each bundle use the current time point.
`wait` advances that time point in a separate instruction. A bundle writes
no GPR and retires once, advancing the PC by four bytes.

### Instruction effects

`cw` prepares the mapped events at the current time point without writing
a GPR. A measurement increments the target qubit's pending count.

A positive `wait` enqueues the pending time point and its events, waits for
acknowledgment, then advances the time point in TCU cycles. A zero interval
leaves the time point and pending events unchanged.

`FMR` enqueues pending events and waits for all accepted measurements of the
selected qubit to complete. It copies the latest result into rd without
changing the measurement register. Further `cw` instructions require a
positive `wait` after `FMR`.

The exit ECALL enqueues pending events and halts the CPU after acknowledgment.
The simulation completes when pending events and result deliveries finish.

[quantum.inc](../examples/common/quantum.inc) provides GNU assembler macros.
`sim_exit` expands to `li a0, 0; li a7, 93; ecall`.

## Instruction and control operation names

| Instruction | `ControlKind` |
| --- | --- |
| `cw` | Codeword |
| `wait` | Wait |
| `FMR` | FetchMeasurement |
| Exit ECALL | Halt |
| `sync` | Synchronize |

## CLI and profile

Start a run with `--config FILE` or `--program FILE`. Run `qsbit-sim --help`
for the executable's option list.

| Option | Meaning |
| --- | --- |
| `--config FILE` | Read a schema-1 JSON run file. Paths inside it are relative to that file. |
| `--program FILE` | Load an ELF program, or raw input when `--raw-base` is supplied. |
| `--cpu-model MODEL` | Select `rv32` (default) or `vliw`. |
| `--backend NAME` | Select a [backend](backends.md); default is mock. Accepts registered names and `module:Class`. |
| `--list-backends` | List discovered backends and missing dependencies. |
| `--help-backend` | Print the selected backend's descriptor and configuration schema. |
| `--backend-schema` | Export an editor schema for backend options. |
| `--generate-config` | Print a run configuration template for the selected backend. |
| `--check-config` | Load and validate the run without executing instructions or writing outputs. |
| `--profile FILE` | Overlay a strict JSON profile on the current settings. |
| `--dump-default-profile FILE` | Write the complete default profile and exit. |
| `--seed N` and `--start TICK` | Override the seed and TCU startup offset, respectively. |
| `--raw-base ADDRESS` | Load raw machine words at this address. |
| `--memory-base ADDRESS` and `--memory-size BYTES` | Select simulated RAM. |
| `--trace FILE` and `--summary FILE` | Write JSONL events and JSON final state. |
| `--inspect ADDRESS` | Include a final 32-bit memory word; repeatable. |
| `--memory-dump FILE` | Write all RAM bytes starting at memory base. |
| `--outcomes 0,1,...` | Set mock bits by measurement ID, starting at one; omitted IDs return zero. |
| `--reset TICK` | Schedule a session reset; repeated ticks must be sorted and unique. |
| `--reverse-registration` | Reverse process registration for diagnostic runs. |
| `--python-path DIRECTORY` | Add a module directory for the selected Python adapter. |

Options are applied from left to right. Later values override earlier ones,
including values loaded by a run file. Within a run file, `profile_file` is
applied before inline `profile`. Unknown options and JSON keys are errors.

| Exit status | Meaning |
| --- | --- |
| 0 | The run completed, or `--help` or `--dump-default-profile` succeeded. |
| 1 | A fault occurred during simulation. |
| 2 | Input, configuration or construction failed. |

An in-simulation fault writes a partial trace and failed summary when the output
paths are usable.

### Run file

A run file combines the program, backend, profile and output paths:

```json
{
  "schema": 1,
  "program": "program.elf",
  "backend": "mock",
  "outcomes": [true],
  "inspect": [4096],
  "trace": "results/feedback.jsonl",
  "summary": "results/feedback.json"
}
```

The program must exist at the path shown, relative to this file.
Output parent directories are created as needed.

| Field | Type and meaning |
| --- | --- |
| `schema` | Required integer `1`. |
| `program` | Program path. |
| `cores` | Per-core IDs, ELF programs, profiles, CPU models and synchronization capacities. Replaces `program`; see [distributed simulation](distributed-simulation.md). |
| `sync_connections` | Neighbor addresses, directional delays in TCU cycles and signal capacities. |
| `cpu_model` | `rv32` (default) or `vliw`. |
| `backend` | Built-in name, registered adapter name or `module:Class`. |
| `backend_options` | Backend-owned options; see [backend configuration](backends.md#discover-and-configure-backends). |
| `simulation` | Optional [repeated-simulation strategy](repeated-simulations.md). |
| `trace_stalls` | Include `CpuStalled` records; boolean, default `true`. |
| `backend_execution` | Batch settings shared by all backends: `max_batch_operations` is an integer from 1 to 4294967295, default 1024. |
| `$schema` | Optional editor schema URI; runtime validates through the selected adapter. |
| `profile_file` | Path to a separate profile overlay. |
| `profile` | Inline profile overlay. |
| `trace`, `summary`, `memory_dump` | Output paths. |
| `python_path` | Optional adapter module directory. |
| `memory_base`, `memory_size`, `raw_base` | RAM addresses and size and optional raw load address. |
| `resets` | Array of integer reset ticks. |
| `inspect` | Array of 32-bit addresses to inspect. |
| `outcomes` | Array of booleans indexed by measurement ID, starting at one; omitted IDs return zero. |
| `reverse_registration` | Boolean diagnostic option. |

### Simulation profile

A simulation profile sets clocks, capacities, communication delays and
codeword mappings. Supply it with `--profile`, or in a run file's `profile`
and `profile_file` fields.

Use `--dump-default-profile` for a complete machine-readable profile.
An overlay changes only the supplied fields.

| Fields | Units or role |
| --- | --- |
| `cpu`, `tcu` | Clock objects with integer `period` and `phase` in nanoseconds. |
| `start`, `watchdog` | Initial TCU start tick, reused as an offset on reset; global simulation watchdog deadline. |
| `memory_latency` | CPU periods from memory acceptance to service completion. |
| `command_latency`, `reply_latency` | Receiver edges for time point and enqueue-reply crossings. |
| `cpu_result_latency`, `fast_result_latency` | Receiver edges for the independent result paths. |
| `timing_capacity`, `event_capacity`, `staging_capacity` | Timing entries, entries per port and staged events. |
| `result_capacity` | Maximum outstanding measurements on each delivery path. |
| `ports`, `qubits`, `firing_width` | Port count, qubit count (1–32) and maximum events per port per time point. |
| `seed`, `fast_feedback` | Random seed and fast-path enable flag. |
| `mappings` | Source port and codeword entries and their event lists. |
| `two_qubit_gates` | Shared gate definitions with two required output endpoints. |

The serialized profile uses `schema: 1`. Each mapping has `port`, `codeword` and
a nonempty `actions` array.

### Codeword mappings

A mapping is looked up by source `port` and `codeword`. Each selected event
has its own physical output `port`; neither port is necessarily a qubit index.
An event selects `kind`, output `port`, `operation`, `targets` and `resources`.
It also supplies `delay` and `duration`. Gates accept `amplitude`; pulses accept
`amplitude` and `axis`; acquisitions accept `discriminator_delay` and `separate_arm`.
Fields belonging to another event kind are rejected. For gates and pulses, `execution_flag` selects
`always` (default), `last_one`, `last_zero` or `equal`. Conditional flags
require a single-qubit gate or pulse and enabled fast feedback. A resource has a numeric
`id` and an `exclusive` boolean. Two overlapping events sharing that resource
conflict if either reservation is exclusive. The same physical output port
cannot host overlapping events, even when their resource declarations differ.
A two-qubit gate can reserve both target resources. Its two configured outputs
share that reservation when their physical intervals coincide. Other events on
the same target may overlap only when both are pulses or one is a discriminator arm.
A measurement sample and an ideal gate starting on the same target at the
same tick also conflict.

Periods and event durations are positive. Discriminator delay may be zero.
Pulse amplitude uses radians/ns; a rotation gate uses radians.
Backend support is checked when an event is requested, so a profile may contain
entries unused by the chosen backend.

### Two-port gate outputs

The default CX requires codeword 6 on port 0 and codeword 10 on port 1 at the
same physical start tick. Both instructions can come from one core. Distributed
programs can place one output on each core.

`two_qubit_gates` defines each gate's unique `name`, backend `operation`, ordered
pair of `targets`, positive `duration` in ns, optional `resources` and exactly two
`inputs`. Each input names `core`, local `port` and `codeword`. The two inputs must
use distinct output ports; an input cannot belong to more than one gate definition.
The single-program core ID is 0. All cores share the same gate definitions.

Each input maps to one unconditional `gate_output` action whose `gate` names the
definition. It uses the mapping's port and inherits the gate duration. `delay`
sets its output latency in ns. Targets and resources come from the shared gate,
not from the output action. An explicit duration must equal the gate duration.

This profile fragment replaces the codeword and two-qubit gate tables:

```json
{
  "two_qubit_gates": [{
    "name": "cx01", "operation": "cx", "targets": [0, 1], "duration": 20,
    "inputs": [
      {"core": 0, "port": 0, "codeword": 6},
      {"core": 0, "port": 1, "codeword": 10}
    ],
    "resources": [{"id": 0}, {"id": 1}]
  }],
  "mappings": [
    {"port": 0, "codeword": 6, "actions": [{"kind": "gate_output", "gate": "cx01"}]},
    {"port": 1, "codeword": 10, "actions": [{"kind": "gate_output", "gate": "cx01"}]}
  ]
}
```

The shared device collects all starts at `fire_tick + delay`, validates both
outputs and applies the ideal gate once at that tick. Missing or misaligned
inputs raise `GateInputMismatch` before the backend changes state at that
boundary. Duplicate outputs on one port raise `ResourceConflict`. The device
does not wait for a late input. Reset discards both active and pending outputs.

Set `two_qubit_gates` to an empty array when replacing the default mappings with
a profile that has no paired gate outputs. Explicit `kind: "gate"` mappings
apply backend gates directly. Neither mode integrates a physical two-qubit pulse
sequence or estimates fidelity loss from waveform misalignment.

## JSONL trace

`CodewordQueued` records codeword preparation; `TimingPointSubmitted`,
`TimingPointEnqueued` and `EnqueueAcknowledged` record the enqueue request, insertion
and acknowledgment of a time point and its events. `TimingPointTriggered` records the
event trigger.

Each line is a JSON object with `schema: 1`. Ticks are nondecreasing and use
nanoseconds. The common fields are `tick`, `epoch`, `kind`, `id`, `label`,
`cycle`, `port`, `codeword`, `operation`, `targets`, `detail` and `value`.
CPU records also use `pc`, `word`, `rd`, `next_pc` and `registers`.

Unused scalars are zero; unused strings and arrays are empty. Interpret a field
according to `kind` rather than treating zero as a missing value.

| Event kind | Meaning and significant fields |
| --- | --- |
| `SessionStarted` | `detail` identifies the profile fingerprint. |
| `InstructionRetired` | Instruction ID, CPU cycle, PC, word, destination, next PC, result and all registers. |
| `CpuStalled` and `PipelineFlushed` | Held instruction and reason, or younger instructions discarded on a branch or exit, respectively. |
| `CodewordQueued` | Instruction ID, planned time point in `cycle`, source port and codeword. |
| `TimingPointSubmitted` | Label and planned time point. |
| `TimingPointEnqueued` | Label, current TCU cycle and post-enqueue timing occupancy in `value`. |
| `EnqueueAcknowledged` | Enqueue label acknowledged to the CPU. |
| `TimingPointTriggered` | Label and TCU cycle. |
| `ConditionCancelled` | Suppressed event ID and label. |
| `CodewordTriggered` | Event ID, label, resolved port, codeword, operation and targets. |
| `OperationStart` | Event identity and duration in `value`. |
| `OperationEnd` | Event ID, label, port, operation and targets. |
| `GateApplied` | One paired gate application; `detail` is the gate name, `operation` and `targets` identify the backend gate, and `value` is its reserved duration. |
| `MeasurementSampled` and `ResultReady` | Measurement ID, target and bit. |
| `MeasurementRegisterUpdated` | Measurement ID, target and bit delivered before the CPU step; FMR can read it on this edge. |
| `ExecutionFlagsUpdated` | Measurement ID, target and bit used to update execution flags after the triggering decision. |
| `MeasurementRegisterRead` | Reading instruction ID, target qubit and returned bit. |
| `EndOfStreamVisible` | Last enqueued label when the TCU receives closure. |
| `SessionReset` and `ResetAborted` | New epoch and aborted event IDs where applicable. |
| `StaleCompletionDiscarded` | An old-epoch completion was ignored. |
| `Fault` | Typed error name in `operation` and explanation in `detail`. |
| `SimulationCompleted` | Successful full-drain stop tick. |

Instruction, label, physical-event and measurement IDs occupy separate
namespaces. Correlate records by event kind, epoch and the appropriate ID.
The `cycle` field is a dimensionless index, not nanoseconds:

| Trace kind | `cycle` meaning | Selected other fields |
| --- | --- | --- |
| `InstructionRetired` | CPU edge index relative to CPU phase | `id`: instruction; `value`: instruction result |
| `CodewordQueued`, `TimingPointSubmitted` | Planned current time point | `CodewordQueued.port` and `codeword`: mapping key |
| `TimingPointEnqueued`, `TimingPointTriggered` | Current logical TCU cycle in this epoch | `TimingPointEnqueued.value`: queue occupancy after enqueue |
| `ConditionCancelled`, `ExecutionFlagsUpdated` | Current logical TCU cycle in this epoch | `id`: canceled event or delivered measurement, respectively |
| Other kinds | Interpret only where explicitly defined; otherwise zero | `id` and `value` depend on `kind` |

### Identity scopes

| Identity | Allocation and scope | Reset behavior |
| --- | --- | --- |
| Epoch | Simulator session identity | Incremented; global time continues |
| Instruction ID | CPU instruction identity within an epoch | CPU instruction sequence restarts |
| Control-event ID | Timing control-generated event identity within an epoch | Sequence restarts |
| Label | Timing control-generated time point identity within an epoch | Sequence restarts; zero marks an empty stream |
| Measurement ID | MeasurementRegisters measurement sequence within an epoch | Sequence restarts |
| Fetch generation | CPU identity for pending instruction fetches | Updated when old fetch work is invalidated |

A measurement reference contains its epoch, measurement ID and target qubit.
Match trace records using their kind, epoch and documented identity.

Independent events at the same tick may be compared as a set; ordering required
on one target still applies. A consumer must reject unknown schema versions.

### Summary file

The JSON summary contains success status, stop tick, `cpu_model`, backend,
resolved backend options and execution settings, complete profile, fingerprint, final CPU registers
and PC, requested memory words, measurement
registers and statevector entries as `[real, imaginary]` pairs.
`measurement_registers` is indexed by qubit. Each entry contains `value`,
`pending` and `valid`; `valid` is true when `pending` is zero.
Backends without statevector inspection return an empty array. A backend may
instead supply `density_matrix` as rows of `[real, imaginary]` entries.
On successful completion, the quantum state includes evolution through `stop_tick`.
