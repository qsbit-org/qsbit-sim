# Program, configuration and trace reference

Use this page to prepare program inputs, configure a run and read its outputs.
The current external JSON formats use schema version 1. C++ declarations are
listed separately in [C++ interfaces](cpp-interfaces.md).

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

The ISA is RV32I plus the custom-0 instructions below. Registers and addresses
are 32 bits; arithmetic wraps as defined by RV32I. ECALL and EBREAK terminate
with distinct traps. Privileged and unselected extensions raise
`IllegalInstruction`. FENCE is supported by the ordered memory model; FENCE.I
is not part of the selected ISA.

## Quantum instruction encoding

Quantum instructions use opcode `0x0b` and the R-type layout:

```text
31          25 24     20 19     15 14   12 11     7 6       0
    funct7       rs2       rs1    funct3     rd       opcode
```

| funct3 | Instruction | Register fields | Completion |
| --- | --- | --- | --- |
| 0 | QAPPEND | rs1: port; rs2: codeword; rd: returned handle. | Actions enter producer staging. Acquisition returns a handle; other actions return zero. |
| 1 | QADVANCE | rs1: unsigned interval; rd and rs2: zero. | Any open group is admitted and acknowledged, then the cursor advances. Zero interval changes nothing. |
| 2 | QFLUSH | rd, rs1 and rs2: zero. | Any open group is admitted and acknowledged. |
| 3 | QREAD | rs1: handle; rd: result destination; rs2: zero. | Flush completes and the CPU-visible bit is consumed. |
| 4 | QEND | rd, rs1 and rs2: zero. | Flush completes and closure is published. Successful completion still waits for drain. |
| 5 | QAPPEND_IF | rs1: port; rs2: codeword; rd: register containing a predicate handle. | A conditional action enters staging; no register is written. |
| 6 | QSYNC | All register fields: zero. | Raises `UnsupportedSynchronization`. |
| 7 | Reserved | Any. | Raises `IllegalInstruction`. |

`funct7` is zero except for QAPPEND_IF, where it is the expected bit, 0 or 1.
Any invalid fixed field makes the encoding illegal.

A measurement handle is a nonzero checked 32-bit reference to a token containing
epoch, measurement ID, slot, generation and target. It is not a qubit number.
QREAD consumes the handle; reset invalidates it. A previously staged fast
condition retains the full token even after CPU consumption.

[quantum.inc](../examples/quantum.inc) supplies GNU `.insn` macros.
Tests independently check the emitted encodings and their execution.
The profile makes no binary-compatibility claim with eQASM or Distributed-HISQ.

## CLI and profile

Start a run with `--config FILE` or `--program FILE`. Run `qsbit-sim --help`
for the executable's option list.

| Option | Meaning |
| --- | --- |
| `--config FILE` | Read a schema-1 JSON run file. Paths inside it are relative to that file. |
| `--program FILE` | Load an ELF program, or raw input when `--raw-base` is supplied. |
| `--backend scripted\|aer\|pulse\|MODULE:CLASS` | Select a [backend](backends.md); default is scripted. |
| `--profile FILE` | Overlay a strict JSON profile on the current settings. |
| `--dump-default-profile FILE` | Write the complete default profile and exit. |
| `--seed N` / `--start TICK` | Override the seed or TCU startup offset. |
| `--raw-base ADDRESS` | Load raw machine words at this address. |
| `--memory-base ADDRESS` / `--memory-size BYTES` | Select simulated RAM. |
| `--trace FILE` / `--summary FILE` | Write JSONL events and JSON final state. |
| `--inspect ADDRESS` | Include a final 32-bit memory word; repeatable. |
| `--memory-dump FILE` | Write all RAM bytes starting at memory base. |
| `--outcomes 0,1,...` | Set scripted bits indexed by measurement issue ID, starting at one. |
| `--reset TICK` | Schedule a session reset; repeated ticks must be sorted and unique. |
| `--reverse-registration` | Reverse process registration for diagnostic runs. |
| `--python-path DIRECTORY` | Add a module directory for the selected Python adapter. |

Options are applied from left to right. Later values override earlier ones,
including values loaded by a run file. Within a run file, `profile_file` is
applied before inline `profile`. Unknown options and JSON keys are errors.

| Exit status | Meaning |
| --- | --- |
| 0 | The run completed and drained. |
| 1 | A fault occurred during simulation. |
| 2 | Input, configuration or construction failed. |

An in-simulation fault writes a partial trace and failed summary when the output
paths are usable.

### Run file

A run file combines the program, backend, profile and output paths:

```json
{
  "schema": 1,
  "program": "feedback.elf",
  "backend": "scripted",
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
| `backend` | Built-in name or `module:Class`. |
| `profile_file` | Path to a separate profile overlay. |
| `profile` | Inline profile overlay. |
| `trace`, `summary`, `memory_dump` | Output paths. |
| `python_path` | Optional adapter module directory. |
| `memory_base`, `memory_size`, `raw_base` | RAM addresses/size and optional raw load address. |
| `resets` | Array of integer reset ticks. |
| `inspect` | Array of 32-bit addresses to inspect. |
| `outcomes` | Array of booleans for scripted measurements. |
| `reverse_registration` | Boolean diagnostic option. |

Program, profile, output and module paths all resolve relative to the run file.

### Timing profile

Use `--dump-default-profile` for a complete machine-readable profile.
An overlay changes only the supplied fields.

| Fields | Units or role |
| --- | --- |
| `cpu`, `tcu` | Clock objects with integer `period` and `phase` in nanoseconds. |
| `start`, `watchdog` | TCU startup offset and global watchdog tick, in nanoseconds. |
| `memory_latency` | CPU periods from memory acceptance to service completion. |
| `command_latency`, `reply_latency` | Receiver edges for group and admission-reply crossings. |
| `cpu_result_latency`, `fast_result_latency` | Receiver edges for the independent result paths. |
| `timing_capacity`, `event_capacity`, `staging_capacity` | Timing entries, entries per port and staged actions. |
| `result_slots`, `history_depth` | CPU result slots and TCU history entries per target. |
| `ports`, `qubits`, `firing_width` | Device size and maximum actions per port per group. |
| `seed`, `fast_feedback` | Random seed and fast-path enable flag. |
| `mappings` | Source port/codeword entries and their action lists. |

The serialized profile uses `schema: 1`. Each mapping has `port`, `codeword` and
a nonempty `actions` array.

### Action mappings

An action selects `kind`, output `port`, `operation`, `targets` and `resources`.
It also supplies `delay` and `duration`, plus `discriminator_delay`,
`amplitude`, `axis` or `separate_arm` as applicable. A resource has a numeric
`id` and an `exclusive` boolean.

Periods and action durations are positive. Discriminator delay may be zero.
Pulse amplitude uses radians/ns; a rotation gate uses radians.
Backend support is checked when an action is requested, so a profile may contain
entries unused by the chosen backend.

## JSONL trace

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
| `CpuStalled` / `PipelineFlushed` | Held instruction and reason, or discarded branch/END work. |
| `ProducerAccepted` | Instruction ID, planned cursor in `cycle`, source port/codeword and returned handle in `value`. |
| `GroupSubmitted` | Label and planned cursor. |
| `GroupAdmitted` | Label, current TCU cycle and post-admission timing occupancy in `value`. |
| `GroupReplyVisible` | Admission label acknowledged to the CPU. |
| `LabelFired` | Label and TCU cycle. |
| `ConditionCancelled` | Suppressed event ID and label. |
| `CodewordTriggered` | Event ID, label, resolved port, codeword, operation and targets. |
| `OperationStart` | Event identity and duration in `value`. |
| `OperationEnd` | Event ID, label, port, operation and targets. |
| `MeasurementSampled` / `ResultReady` | Measurement ID, target and bit. |
| `CpuResultVisible` / `FastResultVisible` | Measurement ID, target and bit at the receiver edge. |
| `ResultConsumed` | Reading instruction ID and returned bit. |
| `EndOfStreamVisible` | Last admitted label when the TCU receives closure. |
| `SessionReset` / `ResetAborted` | New epoch and aborted event IDs where applicable. |
| `StaleCompletionDiscarded` | An old-epoch completion was ignored. |
| `Fault` | Typed error name in `operation` and explanation in `detail`. |
| `SimulationCompleted` | Successful full-drain stop tick. |

Instruction, label, physical-event and measurement IDs occupy separate
namespaces. Correlate records by event kind, epoch and the appropriate ID.
Independent events at the same tick may be compared as a set; ordering required
on one target still applies. A consumer must reject unknown schema versions.

### Summary file

The JSON summary contains success status, stop tick, backend, complete profile,
fingerprint, final registers and PC, requested memory words, unread result slots
and statevector entries as `[real, imaginary]` pairs.
The scripted backend has no statevector.
