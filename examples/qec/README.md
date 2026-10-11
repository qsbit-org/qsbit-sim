# Surface-code memory and logical decoding

Run a distance-3 rotated surface-code memory experiment using qsbit-compiler,
Stim and PyMatching. The compiler emits an RV32I program; the simulator executes
its quantum-control instructions and decoder MMIO accesses.

For a memory graph authored and compiled in Bloq, use the
[Bloq-to-QIR example](../bloq-qir/README.md).

## Setup

Install [qsbit-compiler](https://github.com/qsbit-org/qsbit-compiler#build)
on PATH. From the qsbit-sim repository root:

```sh
uv run --frozen --group build --extra qec cmake --preset clang-ninja -DQSBIT_PYTHON_BACKENDS=ON
cmake --build --preset clang-ninja --parallel
cmake --install build-clang --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

Prepare the C++ dependencies using the [build instructions](../../README.md#build)
before configuring a new checkout.

## Run the surface code

```sh
uv run --frozen --extra qec examples/qec/run.py --output build-clang/qec \
  --rounds 3 --shots 8
```

The runner finds `qsbitc` and `qsbit-sim` on PATH. Use `--compiler PATH` or
`--sim PATH` to select another build.

This example runs a distance-3 Z-memory circuit on 17 qubits with an X error
injected on Stim qubit 1. The controller collects syndrome measurements, requests
a PyMatching correction through decoder MMIO, and applies the returned logical
flip to the raw logical measurement.

The runner independently verifies the decoder output and corrected logical
result using Stim and PyMatching. Every shot must have corrected logical result 0.

Stim generates the circuit and a detector error model with depolarization
probability 0.001 and measurement-flip probability 0.001. Those probabilities
set matching weights. Simulator execution uses ideal gates and the explicitly
injected X error. These runs test known-error correction, not a logical error
rate under sampled noise.

## Outputs and parameters

`build-clang/qec` contains the QIR input, target configuration, compiled artifact
bundle, reference Stim circuit, and each shot's trace, summary and memory dump.
`results.json` records `raw_logical_result`, `decoder_flip`,
`corrected_logical_result`, stop ticks and decoder timing.
Shots use seeds 1 through `shots`.

`--rounds` changes the measurement window length. `--error-qubit` selects the
injected error using Stim's qubit numbering. `--decoder-latency` sets processing
latency in nanoseconds. Its default is 1000; the target also sets a 100 ns
one-way link delay, one byte per nanosecond, and four request and result slots.
The CPU runs at 500 MHz (2 ns period) and the TCU at 250 MHz (4 ns period).
The compiler must support `cpu_period_ns` and `tcu_period_ns` target fields.
All generated settings are saved in `target.json`.

The compiler inserts `wait 0` before measurement reads and decoder calls.
When the timing queue empties, the TCU pauses until subsequent work arrives.
The CPU continues polling the decoder. To exercise a longer decoder delay:

```sh
uv run --frozen --extra qec examples/qec/run.py --output build-clang/qec-delayed \
  --rounds 3 --shots 1 --decoder-latency 100000
```

The correction checks remain unchanged. The trace records `WaitZeroExecuted`,
`TimerPaused` and `TimerResumed`, and the longer delay increases the stop tick.

## Correct data qubits during a loop

The compiler's [repetition-code example](https://github.com/qsbit-org/qsbit-compiler/tree/main/examples/qec)
uses two parity measurements to identify a bit flip on three data qubits.
It applies the decoder's physical correction before the next round and runs
three rounds in an LLVM control-flow loop. Set `QSBIT_COMPILER_SOURCE` to the
compiler checkout, then run these commands from the qsbit-sim repository root:

```sh
export QSBIT_COMPILER_SOURCE="/absolute/path/to/qsbit-compiler"
qsbitc \
  "$QSBIT_COMPILER_SOURCE/examples/qec/repetition.ll" \
  --target "$QSBIT_COMPILER_SOURCE/examples/qec/repetition.target.json" \
  -o build-clang/qec-repetition/repetition.elf
uv run --frozen --extra qec qsbit-run \
  build-clang/qec-repetition/repetition.elf \
  --backend stim --shots 1 \
  --out-dir build-clang/qec-repetition/results
```

The expected output is `110000000000000`: each round records two parity bits
followed by three corrected data-qubit measurements. The first round detects
the injected X error on data qubit 1. The CPU branches on the correction mask
and applies X to that qubit before continuing.

See [decoder feedback](../../docs/decoding.md) for register semantics, bounded
queues, custom algorithms and timing parameters.

## Batched measurement and native reset

The compiler groups consecutive measurements or resets into fixed codeword mappings.
Measurement calls for one layer precede result reads and recording; MR preserves
those measurement records, then resets the layer without measuring it again.
The default three-round experiment produces 33 architectural measurements and
41 single-target reset actions. Groups are split to respect staging and delivery
capacity; grouping does not increase hardware capacity or CPU fetch bandwidth.

This example assumes that control electronics provides native reset, including
any internal feedback without CPU participation. Its configured 80 ns duration is
a modeling assumption (40 ns acquisition, 20 ns discrimination and 20 ns correction),
not a measured hardware latency. The ideal reset channel is applied at the start;
resources remain occupied for the full duration. Internal outcomes are not recorded.

To compare against the same native-reset model without grouping, append
`--no-batch-operations` to the run command and use a different `--output` directory.
Both modes retain the configured clocks, gate durations and `block_cycles`.

Disabling grouping increases instruction demand and may produce `LateAdmission`
with the same timing configuration. This comparison mode does not relax deadlines
or automatically add scheduling slack.
