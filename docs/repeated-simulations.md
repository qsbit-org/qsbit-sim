# Run repeated quantum experiments

Repeated simulations reuse a fixed quantum-operation sequence while preserving
quantum state between measurements. Select a Python backend, install the `experiments` extra and enable
`QSBIT_PYTHON_BACKENDS`. The [AllXY example](../examples/allxy/README.md) includes
a program, configuration and numerical checks.

## Configuration

Add `simulation` to a run configuration and launch it with `qsbit-sim --config FILE`.
Use `--check-config` to validate the ELF, mappings and backend without executing
rounds or writing outputs. Strategy runs accept no other CLI overrides.

```json
{
  "execution": "replay",
  "quantum_execution": "direct",
  "region": {"begin": "round_begin", "end": "round_end"},
  "repetition_count_symbol": "repetitions",
  "repetitions": 1000
}
```

| Field | Meaning |
| --- | --- |
| `execution` | `full` executes every round on the control microarchitecture. `replay` records and checks two three-round runs, then repeats backend calls. |
| `quantum_execution` | `direct` evolves and measures the backend for every shot. `transition_probabilities` uses a supporting backend's single-qubit transition probabilities. Default: `direct`. |
| `region.begin`, `region.end` | ELF symbols delimiting the repeated instructions; begin is inclusive and end is exclusive. |
| `repetition_count_symbol` | File-backed writable 32-bit word loaded by the loop. The runner patches a working copy of the ELF. |
| `repetitions` | Integer from 1 through 4,294,967,295. |

`full` requires `quantum_execution: direct`. The original ELF is unchanged.
Program paths and output paths resolve relative to the run file.
The watchdog must cover the requested total duration, including replayed rounds.

## Program structure

Keep the ELF symbol table and use this RV32I loop:

```asm
.include "quantum.inc"
.section .text
.global _start, round_begin, round_end
_start:
  la t0, repetitions
  lw s0, 0(t0)
round_begin:
  wait.i 40000
  cw.i.i 0, 1
  wait.i 4
  cw.i.i 0, 6
  fmr zero, 0
round_end:
  addi s0, s0, -1
  bnez s0, round_begin
  sim_exit
.section .data
.global repetitions
repetitions: .word 1
```

The codeword numbers must match the configured mappings. The runner verifies the
load, loop suffix and exit instructions. The region accepts only `cw.i.i`,
`wait.i` and `fmr zero`; classical instructions, branches and measurement-result
registers are rejected. This prevents measurement outcomes and loop-carried CPU
data from changing the schedule.

Mappings must select unconditional gates or acquisitions without separate
discriminator arms. Read every acquired target before another codeword or wait,
and finish the region with FMR. Disable `profile.fast_feedback`. All operations
must finish within their round, and the round duration must preserve both clock
phases. Raw images, session resets, memory dumps and a `trace` path override are unsupported.

## Replay and quantum state

The first round retains its startup timing. Later rounds use the steady event
sequence, with absolute times advanced by the verified period. The backend is
reset once, not once per round. Each replayed measurement receives a new identity.

The control checks run with fixed zero and one outcomes. The program restrictions
exclude other control paths; the recorded instructions, queue events and backend
calls must agree. The final two rounds must also have the same backend-call
sequence after subtracting their time offset. Outstanding device operations at a
region boundary cause an error.

`transition_probabilities` requires one qubit, memoryless noise with fixed parameters
and computational-basis projective measurement. Aer supports this option for its circuit adapter. It calculates each
measurement probability from the preceding zero or one result, including all
intervening evolution and gates. Sampling preserves that dependence. This method
has the same probability model as direct evolution but uses a different random
sampling sequence; identical seeds need not produce identical counts across methods.

## Outputs

The `summary` path is required. Its JSON records counts in measurement order within
the round, backend options, the resolved profile, program hash, repetition counts
and host runtime. `control_repetitions_executed` distinguishes actual control runs
from replayed statistics. `control_stop_tick_source` marks controller completion as
`observed` or `extrapolated`.

Control ELF copies, call recordings, traces and summaries are written beside the
result in `<summary-stem>-control/`. Calibration runs omit `CpuStalled` records;
instruction, queue and device events retain their timestamps. Ordinary runs can
set `trace_stalls: false` for the same trace filtering.
