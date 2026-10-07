# QEC with decoder feedback

Run a distance-3 rotated surface-code memory experiment using qsbit-compiler,
Stim and PyMatching. The compiler emits an RV32I program; the simulator executes
its quantum-control instructions and decoder MMIO accesses.

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

The circuit has 17 code qubits and one feedback probe. It initializes a logical
Z memory, injects X on Stim qubit 1, measures three rounds of stabilizers, and
measures the data qubits. The CPU packs measurement records into decoder
requests. PyMatching returns the logical observable flip. The CPU corrects the
recorded logical value and conditionally applies X to the probe, whose final
measurement must equal the decoder bit.

The run checks the detector parities against Stim's measurement converter and
the correction against a separate PyMatching call. Every shot must have
corrected logical value 0 and the expected probe measurement.

Stim generates the circuit and a detector error model with depolarization
probability 0.001 and measurement-flip probability 0.001. Those probabilities
set matching weights. Simulator execution uses ideal gates and the explicitly
injected X error. These runs test known-error correction, not a logical error
rate under sampled noise.

## Outputs and parameters

`build-clang/qec` contains the QIR input, target configuration, compiled artifact
bundle, reference Stim circuit, and each shot's trace, summary and memory dump.
`results.json` records logical and feedback bits, stop ticks and decoder timing.
Shots use seeds 1 through `shots`.

`--rounds` changes the measurement window length. `--error-qubit` selects the
injected error using Stim's qubit numbering. `--decoder-latency` sets processing
latency in nanoseconds. Its default is 1000; the target also sets a 100 ns
one-way link delay, one byte per nanosecond, and four request and result slots.
All generated settings are saved in `target.json`.

The compiler inserts `wait 0` before measurement reads and decoder calls.
When the timing queue empties, the TCU pauses until subsequent work arrives.
The CPU continues polling and branching. To exercise a longer decoder delay:

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
three rounds in an LLVM control-flow loop. Its README includes commands and
expected output bits.

See [decoder feedback](../../docs/decoding.md) for register semantics, bounded
queues, custom algorithms and timing parameters.
