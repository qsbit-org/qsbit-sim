# Surface-code memory and logical decoding

Run a distance-3 rotated surface-code memory experiment using qsbit-compiler,
Stim and PyMatching. The compiler emits an RV32I program; the simulator executes
its quantum-control instructions and decoder MMIO accesses.

For a memory graph authored and compiled in Bloq, use the
[Bloq-to-QIR example](../bloq-qir/README.md).

## Setup

Build [qsbit-compiler](https://github.com/qsbit-org/qsbit-compiler#build) in a
sibling directory. From the qsbit-sim repository root:

```sh
uv sync --frozen --group build --extra qec
cmake --preset clang-ninja -DQSBIT_PYTHON_BACKENDS=ON \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset clang-ninja --parallel
```

Prepare the C++ dependencies using the [build instructions](../../README.md#build)
before configuring a new checkout.

## Run the surface code

```sh
.venv/bin/python examples/qec/run.py \
  --compiler ../qsbit-compiler/build-clang/qsbitc \
  --sim build-clang/qsbit-sim --output build-clang/qec \
  --rounds 3 --shots 8
```

The circuit has 17 qubits. It initializes a logical
Z memory, injects X on Stim qubit 1, measures three rounds of stabilizers, and
measures the data qubits. The CPU packs measurement records into decoder
requests. PyMatching returns the logical observable flip bit. The CPU XORs
this bit with the raw logical measurement to obtain the corrected logical
result and records both the flip bit and corrected result.

The runner computes detector parities with Stim's measurement converter and
checks the decoder flip against a separate PyMatching call. It also checks
the CPU's corrected logical result against the raw logical measurement XOR
the predicted flip. Every shot must have corrected logical result 0.

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
All generated settings are saved in `target.json`.

The compiler inserts `wait 0` before measurement reads and decoder calls.
When the timing queue empties, the TCU pauses until subsequent work arrives.
The CPU continues polling the decoder. To exercise a longer decoder delay:

```sh
.venv/bin/python examples/qec/run.py \
  --compiler ../qsbit-compiler/build-clang/qsbitc \
  --sim build-clang/qsbit-sim --output build-clang/qec-delayed \
  --rounds 3 --shots 1 --decoder-latency 100000
```

The correction checks remain unchanged. The trace records `WaitZeroExecuted`,
`TimerPaused` and `TimerResumed`, and the longer delay increases the stop tick.

## Correct data qubits during a loop

The compiler's [repetition-code example](https://github.com/qsbit-org/qsbit-compiler/tree/main/examples/qec)
uses two parity measurements to identify a bit flip on three data qubits.
It applies the decoder's physical correction before the next round and runs
three rounds in an LLVM control-flow loop. From the qsbit-sim repository root:

```sh
../qsbit-compiler/build-clang/qsbitc \
  ../qsbit-compiler/examples/qec/repetition.ll \
  --target ../qsbit-compiler/examples/qec/repetition.target.json \
  -o build-clang/qec-repetition/repetition.elf
.venv/bin/python ../qsbit-compiler/tools/run.py \
  build-clang/qec-repetition/repetition.elf \
  --sim build-clang/qsbit-sim --backend stim --shots 1 \
  --out-dir build-clang/qec-repetition/results
```

The expected output is `110000000000000`: each round records two parity bits
followed by three corrected data-qubit measurements. The first round detects
the injected X error on data qubit 1. The CPU branches on the correction mask
and applies X to that qubit before continuing.

See [decoder feedback](../../docs/decoding.md) for register semantics, bounded
queues, custom algorithms and timing parameters.
