# Examples

Run commands from the repository root. Follow the [build instructions](../README.md#build)
to create `build-clang/qsbit-sim` and assemble the basic examples.

| Directory | Example | Backend |
| --- | --- | --- |
| [bell-state](bell-state/README.md) | Prepare and measure a Bell pair. | Aer |
| [measurement-feedback](measurement-feedback/README.md) | Select a gate using a measurement result. | Mock or Aer |
| [single-qubit-pulse](single-qubit-pulse/README.md) | Drive a qubit with a constant X pulse. | Pulse |
| [overlapping-pulses](overlapping-pulses/README.md) | Apply X and Z drives simultaneously. | Pulse |
| [quma](quma/README.md) | Reproduce the QuMA AllXY sequence and measurement probabilities. | Aer |
| [eqasm](eqasm/README.md) | Run the eQASM Figure 3 gate sequence and compare scalar and VLIW issue rates. | Aer, Stim or mock |
| [distributed-hisq](distributed-hisq/README.md) | Reproduce neighbor synchronization and scan BISP booking overhead. | Mock |

The basic examples contain `program.S` and `run.json`. CMake places each ELF and
its configuration in `<build-dir>/examples/<example>/`. For a control-only run:

```sh
build-clang/qsbit-sim --config build-clang/examples/measurement-feedback/mock.json
```

The QuMA and eQASM READMEs include dependency setup, experiment commands and result
plots. Numerical backends require the [Python bridge](../docs/backends.md).

## Shared assembly files

[common/quantum.inc](common/quantum.inc) defines GNU assembler macros for scalar
control instructions and 32-bit dual-codeword bundles. [common/link.ld](common/link.ld)
places the entry point at zero and writable data at 0x1000. Compressed instructions
and relaxation are disabled.

The basic run configurations set TCU start to 200 ns. Running an ELF without a
configuration uses the default start of 1000 ns; pass `--start 200` to match the
documented event times.
