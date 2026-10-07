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

## Wait for variable feedback latency

Use `wait 0` before FMR to let the TCU wait for result-dependent work:

```asm
wait.i 8
cw.i.i 0, 4
wait.i 0
fmr t0, 0
beq t0, x0, done
cw.i.i 1, 1
done:
sim_exit
```

Codeword 4 measures qubit 0. Result one selects X on qubit 1; result zero
ends the program without that gate. The empty timing queue pauses the TCU
while the CPU reads the result and selects the branch. After the selected
events are enqueued, the next TCU edge resumes execution. Measurement,
classical computation and submission delays all contribute to physical time.
