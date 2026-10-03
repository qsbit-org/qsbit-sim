# Bell state

[program.S](program.S) applies H to qubit 0, CX to qubits 0 and 1, then measures
both at one time point. The program stores the measurement results at addresses
4096 and 4100.

CX requires two outputs: codeword 6 on port 0 and codeword 10 on port 1.
The program issues both at the same time point. The device validates the pair
and applies CX once; omitting either instruction fails the run.

From the repository root, [build with the Aer backend](../../docs/backends.md)
and run:

```sh
build-clang/qsbit-sim --config build-clang/examples/bell-state/run.json
```

[run.json](run.json) writes `results.json` and `results.jsonl` to
`build-clang/examples/bell-state/`. The ideal result is 00 or 11 with equal
probability; both stored bits must agree. Qubit 0 is the least significant
statevector bit. Reusing the seed with the same adapter and dependencies
reproduces the outcomes.

H starts at 360 ns, CX at 400 ns, and both acquisitions at 440 ns. Measurement
samples the state at 480 ns. Results are ready at 500 ns, CPU-visible at 505 ns
and TCU-visible at 540 ns.
