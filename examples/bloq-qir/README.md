# Bloq surface-code memory through QIR

Compile a Bloq distance-3 X-memory graph, export its VM program as Adaptive
QIR 2.1, compile it with qsbit-compiler and execute it with qsbit-sim. The circuit
uses 17 physical qubits, 33 measurement records, 24 detectors and one logical
observable. PyMatching supplies the logical correction through decoder MMIO.

## Setup

Use a Bloq checkout containing the
[`bloq_qir` exporter](https://github.com/Zhaoyilunnn/bloq/tree/experiment/bloq-qir/bloq_qir)
and install [qsbit-compiler](https://github.com/qsbit-org/qsbit-compiler#build)
on PATH. The exporter build command below uses a sibling Bloq checkout;
`--exporter` selects its executable.

From the qsbit-sim repository root, build the exporter with Bloq's pinned
Rust toolchain and LLVM 21's `opt-21` and `llvm-as-21` available on PATH:

```sh
cargo build --locked --manifest-path ../bloq/Cargo.toml \
  -p bloq_qir --example export
```

From the qsbit-sim repository root, prepare the C++ dependencies using the
[build instructions](../../README.md#build), then enable the QEC backend:

```sh
uv run --frozen --group build --extra qec cmake --preset clang-ninja -DQSBIT_PYTHON_BACKENDS=ON
cmake --build --preset clang-ninja --parallel
cmake --install build-clang --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

## Run the complete example

From the qsbit-sim repository root:

```sh
uv run --frozen --extra qec examples/bloq-qir/run.py \
  --exporter ../bloq/target/debug/examples/export \
  --output build-clang/bloq-qir --shots 4
```

[run.py](run.py) invokes the Rust exporter, builds the device target and decoder
model from the exported QIR and reference circuit, compiles `memory.bc`, then
runs one simulator process per shot. It finds `qsbitc` and `qsbit-sim` on PATH;
`--compiler PATH` and `--sim PATH` select other builds.
Target generation is part of this command.
The simulator executes the physical circuit from the exported Bloq VM program.

Bloq compiles `GalleryItem::XMemory` at distance 3 and lowers it to a VM program.
The exporter adds an explicit logical decoder binding. It submits all 33
measurement records to decoder 0 and records the raw and corrected logical
observables. QIR contains the gates, classical control and decoder calls;
`target.json` supplies port/codeword mappings, durations, decoder transport and
the PyMatching model.

The physical circuit executes ideal gates. The exported reference uses
uniform depolarization probability 0.001 to construct the matching weights.
This example validates ideal memory execution and the decoder interface.
For explicitly injected errors, use the [surface-code QEC example](../qec/README.md).

## Results and artifacts

The command prints `Validated 4 shots, distance 3, 17 qubits` and the path to
`results.json`. Every shot must have simulator `success: true`, program
`exit_code: 0` and corrected logical result 0.

| File in the output directory | Contents |
| --- | --- |
| `memory.ll`, `memory.bc` | Text and bitcode representations of the same Bloq-generated QIR. |
| `memory.vm.json` | The lowered Bloq VM program. |
| `memory.reference.stim` | Reference circuit used for detector conversion and matching weights. |
| `target.json` | Gate mappings and the configured decoder model. |
| `memory.elf` | The compiled RV32I executable. |
| `memory.manifest.json`, `memory.run.json`, `memory.schedule.json`, `memory.lowered.ll` | Compiler artifacts and simulator configuration. |
| `shot-NNNN.run.json`, `shot-NNNN.summary.json`, `shot-NNNN.trace.jsonl`, `shot-NNNN.memory.bin` | Each shot's settings, final state, events and RAM dump. |
| `results.json` | Output counts, raw logical results, independently predicted decoder flips, corrected results and decoder timing. |
| `export/` | The exporter output, including its other verification workloads. |

Each output bitstring contains 35 bits: 33 measurement records, the raw logical
observable, then the corrected observable. Measurement records can vary across
shots; corrected observables must be zero. Seeds run from 1 through `shots`.

The runner converts the simulator measurements to detectors with Stim and
predicts the correction with a separate PyMatching decoder built from the
reference detector error model. It checks the recorded raw parity and corrected
result, absence of detector events in this ideal circuit, exactly one decoder
job, processing latency and result-return ordering.

## Run the stages separately

After the complete command has generated the files, compile the readable LLVM
text and execute the compiler's run configuration directly:

```sh
qsbitc \
  build-clang/bloq-qir/memory.ll \
  --target build-clang/bloq-qir/target.json \
  -o build-clang/bloq-qir/memory.elf
uv run --frozen --extra qec qsbit-sim --config build-clang/bloq-qir/memory.run.json --backend stim
```

`memory.bc` is LLVM bitcode, not machine code. Both `.ll` and `.bc` are compiler
inputs; the simulator executes `memory.elf`. Direct simulator execution writes
the summary specified by its run configuration. Use the complete runner to
collect and validate the logical output buffer.

To exercise LLVM text input and a longer terminal decoder wait:

```sh
uv run --frozen --extra qec examples/bloq-qir/run.py \
  --exporter ../bloq/target/debug/examples/export \
  --output build-clang/bloq-qir-delayed --shots 1 \
  --qir-format ll --decoder-latency 100000
```

Decoder latency is in nanoseconds. The compiler inserts `wait 0` around
measurement reads and decoder calls. This memory workload waits for its terminal
correction; it does not request extra protection rounds during that wait.

## Tests

See [Bloq example testing](../../docs/engineering-and-testing.md#bloq-example)
for CTest configuration and cross-project coverage.
