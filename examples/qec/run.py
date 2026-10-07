#!/usr/bin/env python3
"""Compile and run a surface-code memory experiment with logical decoding."""

import argparse
import json
import struct
import subprocess
from pathlib import Path

import numpy as np
import pymatching
import stim


def pointer(index):
    return "ptr null" if index == 0 else f"ptr inttoptr (i64 {index} to ptr)"


def experiment(rounds, error_qubit):
    circuit = stim.Circuit.generated(
        "surface_code:rotated_memory_z",
        distance=3,
        rounds=rounds,
        after_clifford_depolarization=0.001,
        before_measure_flip_probability=0.001,
    ).flattened()
    matching = pymatching.Matching.from_detector_error_model(
        circuit.detector_error_model(decompose_errors=True)
    )
    edges = list(matching.to_networkx().edges(data=True))
    checks = np.zeros((circuit.num_detectors, len(edges)), dtype=np.uint8)
    observables = np.zeros((circuit.num_observables, len(edges)), dtype=np.uint8)
    weights = []
    for column, (a, b, data) in enumerate(edges):
        for node in (a, b):
            if node < circuit.num_detectors:
                checks[node, column] ^= 1
        for observable in data["fault_ids"]:
            observables[observable, column] = 1
        weights.append(data["weight"])
    used = sorted({t.value for op in circuit for t in op.targets_copy() if t.is_qubit_target})
    qubits = {q: i for i, q in enumerate(used)}
    detector_rows, logical = [], []
    measurements = 0
    operations = []
    reference = stim.Circuit()
    injected = False
    for op in circuit:
        name, targets = op.name, op.targets_copy()
        if name in {"DEPOLARIZE1", "DEPOLARIZE2", "X_ERROR"}:
            continue
        if name == "DETECTOR":
            detector_rows.append([measurements + t.value for t in targets])
            reference.append(op)
            continue
        if name == "OBSERVABLE_INCLUDE":
            logical.extend(measurements + t.value for t in targets)
            reference.append(op)
            continue
        if name in {"TICK", "QUBIT_COORDS", "SHIFT_COORDS"}:
            continue
        if name not in {"R", "H", "CX", "MR", "M"}:
            raise ValueError(f"unsupported generated instruction {name}")
        if name != "R" and not injected:
            if error_qubit is not None:
                if error_qubit not in qubits:
                    raise ValueError("error-qubit is not used by this circuit")
                operations.append(("x", [qubits[error_qubit]], None))
            injected = True
        values = [qubits[t.value] for t in targets]
        if name == "CX":
            operations.extend(("cx", values[i : i + 2], None) for i in range(0, len(values), 2))
        else:
            for q in values:
                if name in {"MR", "M"}:
                    operations.append(("measure", [q], measurements))
                    measurements += 1
                    if name == "MR":
                        operations.append(("reset", [q], None))
                else:
                    operations.append(("reset" if name == "R" else "h", [q], None))
        reference.append(op)
    detectors = np.zeros((len(detector_rows), measurements), dtype=np.uint8)
    for row, indices in enumerate(detector_rows):
        for index in indices:
            detectors[row, index] ^= 1
    options = {
        "check_matrix": checks.tolist(),
        "observables": observables.tolist(),
        "measurement_to_detector": detectors.tolist(),
        "weights": weights,
    }
    return operations, len(qubits), measurements, logical, options, reference


def generate(directory, rounds, error_qubit, latency):
    operations, qubits, count, logical, options, reference = experiment(rounds, error_qubit)
    pairs = sorted({tuple(q) for name, q, _ in operations if name == "cx"})
    mappings = []
    for q in range(qubits):
        for code, name in enumerate(("x", "h", "z", "measure"), 1):
            mappings.append(
                {
                    "operation": name,
                    "qubits": [q],
                    "port": q,
                    "codeword": code,
                    "duration_ns": 40 if name == "measure" else 20,
                }
            )
    for i, pair in enumerate(pairs, 10):
        mappings.append(
            {
                "operation": "cx",
                "qubits": list(pair),
                "port": pair[0],
                "codeword": i,
                "duration_ns": 40,
            }
        )
    decoding = {
        "mmio_base": 0x40000000,
        "request_capacity": 4,
        "result_capacity": 4,
        "link_latency": 100,
        "bytes_per_tick": 1,
        "packet_overhead": 16,
        "decoders": [
            {
                "id": 0,
                "measurements": count,
                "outputs": 1,
                "latency": latency,
                "initiation_interval": 1000,
                "backend": "pymatching",
                "options": options,
            }
        ],
    }
    target = {
        "schema": 1,
        "name": "qec-surface-3",
        "qubits": qubits,
        "ports": qubits,
        "start_ns": 10000,
        "block_cycles": 1000,
        "mappings": mappings,
        "decoding": decoding,
    }
    declarations = [
        f"declare void @__quantum__qis__{name}__body(ptr)" for name in ("h", "x", "z", "reset")
    ]
    declarations += [
        "declare void @__quantum__qis__cx__body(ptr, ptr)",
        "declare void @__quantum__qis__mz__body(ptr, ptr)",
        "declare i1 @__quantum__rt__read_result(ptr)",
        "declare void @__quantum__rt__result_record_output(ptr, ptr)",
        "declare void @__quantum__rt__bool_record_output(i1, ptr)",
        "declare void @reset_decoder_ui64(i64)",
        "declare void @enqueue_syndromes_ui64(i64, i64, i64, i64)",
        "declare i64 @get_corrections_ui64(i64, i64, i64)",
    ]
    lines = declarations + [
        "define void @qec() #0 {",
        "entry:",
        "call void @reset_decoder_ui64(i64 0)",
    ]
    chunk = []

    def enqueue():
        if not chunk:
            return
        suffix = chunk[-1]
        value = "0"
        for shift, index in enumerate(chunk):
            lines.extend(
                [
                    f"%wide{index} = zext i1 %m{index} to i64",
                    f"%shift{index} = shl i64 %wide{index}, {shift}",
                    f"%pack{index} = or i64 {value}, %shift{index}",
                ]
            )
            value = f"%pack{index}"
        lines.append(
            f"call void @enqueue_syndromes_ui64(i64 0, i64 {len(chunk)}, i64 {value}, i64 {suffix})"
        )
        chunk.clear()

    for name, qs, result in operations:
        if name == "measure":
            lines.extend(
                [
                    f"call void @__quantum__qis__mz__body({pointer(qs[0])}, {pointer(result)})",
                    f"%m{result} = call i1 @__quantum__rt__read_result({pointer(result)})",
                    f"call void @__quantum__rt__result_record_output({pointer(result)}, ptr null)",
                ]
            )
            chunk.append(result)
            if len(chunk) == 8:
                enqueue()
        else:
            lines.append(
                f"call void @__quantum__qis__{name}__body({', '.join(pointer(q) for q in qs)})"
            )
    enqueue()
    lines.extend(
        [
            "%correction = call i64 @get_corrections_ui64(i64 0, i64 1, i64 0)",
            "%flip = trunc i64 %correction to i1",
        ]
    )
    value = "false"
    for i, index in enumerate(logical):
        lines.append(f"%observable{i} = xor i1 {value}, %m{index}")
        value = f"%observable{i}"
    lines.extend(
        [
            f"%corrected = xor i1 {value}, %flip",
            "call void @__quantum__rt__bool_record_output(i1 %corrected, ptr null)",
            "call void @__quantum__rt__bool_record_output(i1 %flip, ptr null)",
            "ret void",
            "}",
            f'attributes #0 = {{ "entry_point" "qir_profiles"="adaptive_profile" "required_num_qubits"="{qubits}" "required_num_results"="{count}" }}',
        ]
    )
    (directory / "memory.ll").write_text("\n".join(lines) + "\n")
    (directory / "target.json").write_text(json.dumps(target, indent=2) + "\n")
    (directory / "reference.stim").write_text(str(reference))
    return count, logical, options, reference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--sim", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/qec"))
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--shots", type=int, default=8)
    parser.add_argument("--error-qubit", type=int, default=1)
    parser.add_argument("--decoder-latency", type=int, default=1000)
    args = parser.parse_args()
    if args.rounds < 1 or args.shots < 1:
        parser.error("rounds and shots must be positive")
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    count, logical, options, reference = generate(
        directory, args.rounds, args.error_qubit, args.decoder_latency
    )
    subprocess.run(
        [
            str(args.compiler.resolve()),
            str(directory / "memory.ll"),
            "--target",
            str(directory / "target.json"),
            "-o",
            str(directory / "memory.elf"),
        ],
        check=True,
    )
    base = json.loads((directory / "memory.run.json").read_text())
    matching = pymatching.Matching(
        np.array(options["check_matrix"], dtype=np.uint8),
        weights=options["weights"],
        faults_matrix=np.array(options["observables"], dtype=np.uint8),
    )
    converter = reference.compile_m2d_converter()
    records = []
    for shot in range(args.shots):
        run = dict(base, backend="stim", trace_stalls=False)
        run["profile"] = dict(base["profile"], seed=shot + 1)
        run["trace"] = f"shot-{shot}.trace.jsonl"
        run["summary"] = f"shot-{shot}.summary.json"
        run["memory_dump"] = f"shot-{shot}.memory.bin"
        config = directory / f"shot-{shot}.json"
        config.write_text(json.dumps(run, indent=2) + "\n")
        result = subprocess.run(
            [str(args.sim.resolve()), "--config", str(config)], capture_output=True, text=True
        )
        summary = json.loads((directory / run["summary"]).read_text())
        if result.returncode:
            raise RuntimeError(
                f"simulation failed: {summary.get('fault')} {summary.get('fault_message')}\n{result.stderr}"
            )
        memory = (directory / run["memory_dump"]).read_bytes()
        length = struct.unpack_from("<I", memory, 0x10000)[0]
        values = struct.unpack_from(f"<{length}I", memory, 0x10004)
        if length != count + 2:
            raise AssertionError("incorrect QIR output record count")
        bits = np.array([values[:count]], dtype=np.bool_)
        detectors, observables = converter.convert(measurements=bits, separate_observables=True)
        predicted = int(matching.decode(detectors[0])[0])
        raw_logical = int(observables[0, 0])
        expected = raw_logical ^ predicted
        if values[-2] != expected or values[-1] != predicted:
            raise AssertionError("decoder flip or corrected logical result differs from reference")
        if expected:
            raise AssertionError("known single-qubit error was not corrected")
        events = [json.loads(line) for line in (directory / run["trace"]).read_text().splitlines()]
        selected = {
            event["kind"]: event["tick"]
            for event in events
            if event["kind"] in {"DecoderStarted", "DecoderCompleted", "DecoderResultReturned"}
        }
        records.append(
            {
                "shot": shot,
                "raw_logical_result": raw_logical,
                "decoder_flip": values[-1],
                "corrected_logical_result": values[-2],
                "stop_tick": summary["stop_tick"],
                **selected,
            }
        )
    (directory / "results.json").write_text(json.dumps(records, indent=2) + "\n")
    print(
        f"Validated {args.shots} shots, distance 3, {args.rounds} rounds: {directory / 'results.json'}"
    )


if __name__ == "__main__":
    main()
