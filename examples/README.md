# Examples

Run commands from the repository root. Follow the [build instructions](../README.md#build)
to create `build-clang/qsbit-sim` and assemble the Bell-state and measurement-feedback examples.

| Directory | Example | Backend |
| --- | --- | --- |
| [bell-state](bell-state/README.md) | Prepare and measure a Bell pair. | Aer |
| [measurement-feedback](measurement-feedback/README.md) | Select a gate using a measurement result. | Mock or Aer |
| [bloq-qir](bloq-qir/README.md) | Export a Bloq d3 surface-code memory to QIR, compile it and decode its logical result. | Stim and PyMatching |
| [qec](qec/README.md) | Compile QIR, decode surface-code measurements and correct the logical result. | Stim and PyMatching |
| [qutip](qutip/README.md) | Run pulse calibration, relaxation, feedback, readout and tunable-coupler experiments. | QuTiP |
| [quma](quma/README.md) | Reproduce the QuMA AllXY sequence and measurement probabilities. | Aer |
| [eqasm](eqasm/README.md) | Run the eQASM Figure 3 gate sequence and compare scalar and VLIW issue rates. | Aer, Stim or mock |
| [distributed-hisq](distributed-hisq/README.md) | Recreate neighbor synchronization and scan booking overhead for BISP, the booking-based synchronization protocol from Distributed-HISQ. | Mock |

The Bell-state and measurement-feedback examples contain `program.S` and `run.json`.
CMake places each ELF and
its configuration in `<build-dir>/examples/<example>/`. For a control-only run:

```sh
build-clang/qsbit-sim --config build-clang/examples/measurement-feedback/mock.json
```

Each experiment README documents its additional dependencies and commands.
Numerical backends require the [Python bridge](../docs/backends.md).

## Shared assembly files

[common/quantum.inc](common/quantum.inc) defines GNU assembler macros for scalar
control instructions and 32-bit dual-codeword bundles. [common/link.ld](common/link.ld)
places the entry point at zero and writable data at 0x1000. Compressed instructions
and assembler and linker relaxation are disabled.

The Bell-state and measurement-feedback run configurations set TCU start to 200 ns.
Running an ELF without a
configuration uses the default start of 1000 ns; pass `--start 200` to match the
documented event times.
