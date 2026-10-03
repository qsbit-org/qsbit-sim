# Measurement feedback

[program.S](program.S) applies X to qubit 0, measures it, waits through FMR and
takes an RV32I branch. Result one schedules X on qubit 1; result zero schedules Z.
The program stores the result at address 4096.

From the repository root, [build the simulator](../../README.md#build)
and run the [mock configuration](mock.json):

```sh
build-clang/qsbit-sim --config build-clang/examples/measurement-feedback/mock.json
```

The configured outcome is one. Results are written to `mock-results.json` and
`mock-results.jsonl` in `build-clang/examples/measurement-feedback/`. To take the
other branch, add `--outcomes 0`.

With the [Aer backend](../../docs/backends.md), use [run.json](run.json):

```sh
build-clang/qsbit-sim --config build-clang/examples/measurement-feedback/run.json
```

Aer returns one for this preparation, yielding `|11>`. Its outputs are
`results.json` and `results.jsonl` in the same directory.

X on qubit 0 starts at 360 ns and acquisition at 440 ns. The CPU receives the
result at 505 ns. `wait` advances the time point from cycle 12 to cycle 26.
Both branches enqueue the selected event at 700 ns for output at 720 ns.
