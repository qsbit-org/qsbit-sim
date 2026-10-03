# Single-qubit pulse

[program.S](program.S) selects the default X-drive descriptor: duration 20 ns
and amplitude pi/20 radians/ns. The pulse backend evolves with
`H = amplitude * X / 2`, then measures. The result is one, stored at address 4096.

From the repository root, [build with the pulse backend](../../docs/backends.md)
and run:

```sh
build-clang/qsbit-sim --config build-clang/examples/single-qubit-pulse/run.json
```

[run.json](run.json) writes `results.json` and `results.jsonl` to
`build-clang/examples/single-qubit-pulse/`. The X drive starts at 360 ns,
acquisition at 440 ns and measurement samples the state at 480 ns.

The Aer circuit backend rejects this program with `UnsupportedCapability`
before pulse execution.
