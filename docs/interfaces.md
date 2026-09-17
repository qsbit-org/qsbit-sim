# Version 1 program, configuration and trace interfaces

## Program input

ELF input must be a little-endian ELF32 RISC-V executable with uncompressed instruction
flags, valid nonoverlapping PT_LOAD segments and an aligned executable entry. Segment
permissions are enforced. Unmapped bytes inside configured RAM may hold data but cannot
be fetched as instructions. Segment BSS is zero-filled. Raw input requires an explicit
`--raw-base` and a nonempty multiple of four bytes. All memory accesses are little-endian;
misaligned halfword, word or instruction accesses produce typed faults.

The ISA is RV32I plus the custom-0 profile below. Registers and addresses are 32 bits.
Arithmetic wraps as specified by RV32I; simulation time and protocol IDs never wrap.
ECALL and EBREAK are distinct terminating traps. Privileged and unselected extensions
produce `IllegalInstruction`. FENCE is supported by the ordered memory model.

## Quantum instruction encoding

All quantum instructions use R-type fields and opcode `0x0b`. Fields are funct7[31:25],
rs2[24:20], rs1[19:15], funct3[14:12], rd[11:7], opcode[6:0].

| funct3 | Mnemonic | Fields | Completion |
| --- | --- | --- | --- |
| 0 | QAPPEND | rs1=port register, rs2=codeword register, rd=handle destination; funct7=0. | Bounded producer acceptance; nonmeasurement writes zero. |
| 1 | QADVANCE | rs1=unsigned interval register; rd=rs2=funct7=0. | Seal/admit old group if required, then move cursor. Zero interval is a no-op. |
| 2 | QFLUSH | rd=rs1=rs2=funct7=0. | Matching group acknowledgment, or immediate if nothing is open. |
| 3 | QREAD | rs1=handle register, rd=result destination; rs2=funct7=0. | Flush, wait for CPU-visible result, consume handle, write bit. |
| 4 | QEND | rd=rs1=rs2=funct7=0. | Flush and publish closure; global stop additionally waits for drain. |
| 5 | QAPPEND_IF | rd=predicate-handle source, rs1=port, rs2=codeword; funct7=expected bit 0 or 1. | Accept staged action with an exact-token predicate; no register write. |
| 6 | QSYNC | All fields zero except opcode and funct3. | `UnsupportedSynchronization` in v1. |
| 7 | Reserved | Any fields. | `IllegalInstruction`. |

Invalid fixed fields are illegal instructions. A measurement handle is a nonzero checked
32-bit reference to an epoch, measurement ID, slot, generation and target. It is not a
qubit number. Consuming or resetting a handle invalidates later QREAD attempts. A staged
fast condition retains the full identity even if its CPU slot is later consumed.

`examples/quantum.inc` is the encoding source for GNU `.insn` clients. Native tests
independently check decoding masks, disassembled programs and execution. No binary
compatibility with Distributed-HISQ or eQASM is claimed.

## CLI and profile

Provide either `--config FILE` or `--program FILE`. Useful options:

| Option | Meaning |
| --- | --- |
| `--config FILE` | Load a complete JSON run configuration; paths in it are relative to that file. |
| `--backend scripted|aer|pulse` | Select numerical capability; default scripted. |
| `--profile FILE` | Apply a strict JSON profile overlay to the current defaults. |
| `--dump-default-profile FILE` | Write the complete current profile and exit. |
| `--seed N`, `--start TICK` | Override seed or epoch startup offset. |
| `--raw-base ADDRESS` | Treat program as raw machine words at this address. |
| `--memory-base ADDRESS`, `--memory-size BYTES` | Select contiguous simulated RAM. |
| `--trace FILE`, `--summary FILE` | JSONL events and JSON final state. |
| `--inspect ADDRESS` | Add a final 32-bit memory word to the summary; repeatable. |
| `--memory-dump FILE` | Write all configured RAM bytes, beginning at memory-base. |
| `--outcomes 0,1,...` | Scripted outcomes indexed by measurement issue ID starting at one. |
| `--reset TICK` | Schedule a global session reset; repeatable, sorted and unique. |
| `--reverse-registration` | Diagnostic process-registration permutation. |
| `--python-path DIRECTORY` | Python module search directory for the selected adapter. |

Arguments apply from left to right; later CLI values override a run file, and a later
profile can override an earlier individual option. Unknown options and configuration keys
reject. Exit status is 0 for complete drain,
1 for an in-simulation fault, and 2 for invalid input, configuration or construction.
A run-time fault writes its partial trace and a failed summary when output paths work.

Run configuration schema 1 accepts `program`, `backend`, `profile` (inline object),
`profile_file` (separate JSON overlay), `trace`, `summary`, `memory_dump`,
`python_path`, `memory_base`, `memory_size`, `raw_base`, `resets` (integer ticks),
`inspect` (32-bit addresses), `outcomes` (booleans indexed by measurement issue ID),
and `reverse_registration` (boolean). `schema: 1` is required. The separate profile
is applied before the inline profile. Program, profile, output, and Python-module paths
resolve relative to the run file. Output parent directories are created if needed.

Profile schema 1 includes `cpu` and `tcu` period/phase objects; `start`, `watchdog`;
`memory_latency`, `command_latency`, `reply_latency`, `cpu_result_latency`,
`fast_result_latency`; `timing_capacity`, `event_capacity`, `staging_capacity`,
`result_slots`, `history_depth`, `ports`, `qubits`, `firing_width`, `seed`, `fast_feedback`
and `mappings`. A mapping has source `port`, `codeword` and nonempty `actions`.

Each action declares `kind`, output `port`, `operation`, `targets`, `resources`, `delay`,
`duration`, `discriminator_delay`, `amplitude`, `axis` and `separate_arm` as applicable.
A resource has numeric `id` and boolean `exclusive`. See the dumped default for a complete
machine-readable example. Durations and clock periods are positive; discriminator delay
may be zero. Unsupported numerical actions reject when a program requests them, so a
shared map can contain entries unused by a selected backend.

## JSONL trace

Every line is a schema-1 object. Global fields are `tick` (nanoseconds), `epoch`, `kind`,
`id`, `label`, `cycle`, `port`, `codeword`, `operation`, `targets`, `detail` and `value`.
CPU fields are `pc`, `word`, `rd`, `next_pc`, `registers`. Unused scalar fields are zero;
unused strings and arrays are empty. Interpret fields according to event kind rather
than treating zero as a missing ID. The trace is nondecreasing in global tick.

| Event | Significant fields |
| --- | --- |
| SessionStarted | detail is profile fingerprint. |
| InstructionRetired | id, CPU cycle, PC, word, destination, next PC, value and all 32 registers. |
| CpuStalled / PipelineFlushed | Held instruction ID and reason, or a branch/END flush. |
| ProducerAccepted | Instruction ID, cursor in cycle, source port/codeword, returned handle in value. |
| GroupSubmitted | Label and producer cursor. |
| GroupAdmitted | Label, current TCU cycle and post-admission timing-queue occupancy in value. |
| GroupReplyVisible | Label whose acknowledgment reached the CPU. |
| LabelFired | Label and TCU cycle. |
| ConditionCancelled | Suppressed event ID and label. |
| CodewordTriggered | Event ID, label, resolved port, codeword, operation and targets. |
| OperationStart | Same event identity; value is duration in ticks. |
| OperationEnd | Event ID, label, port, operation and targets. |
| MeasurementSampled / ResultReady | Measurement ID, target and value bit. |
| CpuResultVisible / FastResultVisible | Measurement ID, target and value bit at the corresponding receiver edge. |
| ResultConsumed | Reading instruction ID and returned bit. |
| EndOfStreamVisible | Last admitted label at the TCU closure boundary. |
| SessionReset / ResetAborted | New epoch; aborted event IDs where applicable. |
| StaleCompletionDiscarded | An old-epoch completion was ignored. |
| Fault | operation is typed error name; detail is explanation. |
| SimulationCompleted | Successful full-drain stop tick. |

IDs occupy separate namespaces: instruction, label, physical event and measurement.
Use event kind plus epoch and ID for correlation. Independent same-time outputs may be
compared as a set; ordered events on one target retain their times and issue identities.
A trace consumer must reject unknown schema versions rather than infer a changed layout.

The JSON summary stores success, stop tick, selected backend, complete configuration,
fingerprint, final registers and PC, requested memory words, unconsumed result slots and
complex statevector entries as `[real, imaginary]`. Scripted backends have no statevector.
