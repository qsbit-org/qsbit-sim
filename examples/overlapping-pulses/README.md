# Overlapping pulses

[program.S](program.S) starts X and Z drives together on qubit 0.
[profile.json](profile.json) assigns them separate ports and a shared
nonexclusive resource. The pulse backend integrates their sum for 20 ns.

From the repository root, [build with the pulse backend](../../docs/backends.md)
and run:

```sh
build-clang/qsbit-sim --config build-clang/examples/overlapping-pulses/run.json
```

[run.json](run.json) writes `results.json` and `results.jsonl` to
`build-clang/examples/overlapping-pulses/`. Both drives start at 360 ns.
The final state is `-i (|0> + |1>) / sqrt(2)` on qubit 0; qubit 1 remains zero.
