#!/usr/bin/env python3
"""Export Bloq d3 X-memory to QIR, compile it and validate logical decoding."""

import argparse
import json
import re
import shutil
import struct
import subprocess
from collections import Counter
from pathlib import Path

import numpy as np
import pymatching
import stim


def mappings_for(text, qubits):
    operations = set()
    names = {"h", "x", "z", "s", "t", "cx", "mz", "reset"}
    for name, variant, arguments in re.findall(
        r"call void @__quantum__qis__(\w+)__(body|adj)\(([^\n]*)\)", text
    ):
        if name not in names or (variant == "adj" and name not in {"s", "t"}):
            raise ValueError(f"unsupported exported operation {name}__{variant}")
        identifiers = [
            int(value) if value else 0
            for value in re.findall(r"ptr (?:null|inttoptr \(i64 (\d+) to ptr\))", arguments)
        ]
        if len(identifiers) != (2 if name in {"cx", "mz"} else 1):
            raise ValueError("exported operation requires static resource identifiers")
        operands = tuple(identifiers[:2] if name == "cx" else identifiers[:1])
        if any(qubit >= qubits for qubit in operands):
            raise ValueError("exported operation exceeds the declared qubit count")
        operation = {"mz": "measure", "reset": "measure"}.get(name, name)
        if variant == "adj":
            operation += "dg"
        operations.add((operation, operands))
        if name == "reset":
            operations.add(("x", operands))
    if not operations:
        raise ValueError("exported QIR contains no quantum operations")
    return [
        {
            "operation": operation,
            "qubits": list(operands),
            "port": operands[0],
            "codeword": codeword,
            "duration_ns": 40 if operation == "measure" else 20,
        }
        for codeword, (operation, operands) in enumerate(sorted(operations), 1)
    ]


def decoder_options(reference):
    matching = pymatching.Matching.from_detector_error_model(
        reference.detector_error_model(decompose_errors=True)
    )
    edges = list(matching.to_networkx().edges(data=True))
    checks = np.zeros((reference.num_detectors, len(edges)), dtype=np.uint8)
    observables = np.zeros((reference.num_observables, len(edges)), dtype=np.uint8)
    weights = []
    for column, (a, b, data) in enumerate(edges):
        for node in (a, b):
            if node < reference.num_detectors:
                checks[node, column] ^= 1
        for observable in data["fault_ids"]:
            observables[observable, column] = 1
        weights.append(data["weight"])
    rows = np.zeros((reference.num_detectors, reference.num_measurements), dtype=np.uint8)
    count, detector = 0, 0
    for operation in reference.flattened():
        if operation.name == "DETECTOR":
            for operand in operation.targets_copy():
                rows[detector, count + operand.value] ^= 1
            detector += 1
        else:
            count += stim.Circuit(str(operation)).num_measurements
    return {
        "check_matrix": checks.tolist(),
        "observables": observables.tolist(),
        "measurement_to_detector": rows.tolist(),
        "weights": weights,
    }


def generate(directory, exporter, latency):
    exports = directory / "export"
    subprocess.run([str(exporter), str(exports)], check=True)
    workloads = json.loads((exports / "workloads.json").read_text())
    memory = next(workload for workload in workloads if workload["name"] == "memory")
    for name in ("memory.ll", "memory.bc", "memory.vm.json", "memory.reference.stim"):
        shutil.copyfile(exports / name, directory / name)
    reference = stim.Circuit((directory / "memory.reference.stim").read_text())
    if (memory["qubits"], reference.num_measurements, reference.num_detectors) != (17, 33, 24):
        raise ValueError("this example requires the Bloq d3 X-memory export")
    if reference.num_observables != 1:
        raise ValueError("the memory export must have one logical observable")
    target = {
        "schema": 1,
        "name": "bloq-surface-memory-3",
        "qubits": memory["qubits"],
        "ports": memory["qubits"],
        "start_ns": 10000,
        "block_cycles": 1000,
        "mappings": mappings_for((directory / "memory.ll").read_text(), memory["qubits"]),
        "decoding": {
            "mmio_base": 0x40000000,
            "request_capacity": 4,
            "result_capacity": 4,
            "link_latency": 100,
            "bytes_per_tick": 1,
            "packet_overhead": 16,
            "decoders": [
                {
                    "id": 0,
                    "measurements": reference.num_measurements,
                    "outputs": 1,
                    "latency": latency,
                    "initiation_interval": 1000,
                    "backend": "pymatching",
                    "options": decoder_options(reference),
                }
            ],
        },
    }
    (directory / "target.json").write_text(json.dumps(target, indent=2) + "\n")
    return reference


def simulate(directory, simulator, shots, reference, latency):
    base = json.loads((directory / "memory.run.json").read_text())
    manifest = json.loads((directory / "memory.manifest.json").read_text())
    layout = manifest["output_buffer"]
    converter = reference.compile_m2d_converter()
    oracle = pymatching.Matching.from_detector_error_model(
        reference.detector_error_model(decompose_errors=True)
    )
    records, counts = [], Counter()
    for shot in range(shots):
        prefix = f"shot-{shot:04d}"
        run = dict(base, backend="stim", trace_stalls=False)
        run["profile"] = dict(base["profile"], seed=shot + 1)
        run["trace"] = f"{prefix}.trace.jsonl"
        run["summary"] = f"{prefix}.summary.json"
        run["memory_dump"] = f"{prefix}.memory.bin"
        config = directory / f"{prefix}.run.json"
        config.write_text(json.dumps(run, indent=2) + "\n")
        result = subprocess.run(
            [str(simulator), "--config", str(config)], capture_output=True, text=True
        )
        summary = json.loads((directory / run["summary"]).read_text())
        if result.returncode or not summary["success"]:
            raise RuntimeError(
                f"simulation failed: {summary.get('fault')} {summary.get('fault_message')}\n"
                f"{result.stderr}"
            )
        if summary["exit_code"] != 0 or any(
            register["pending"] for register in summary["measurement_registers"]
        ):
            raise AssertionError("program failed or left unfinished measurements")
        memory = (directory / run["memory_dump"]).read_bytes()
        length = struct.unpack_from("<I", memory, layout["count_address"])[0]
        if length != reference.num_measurements + 2:
            raise AssertionError("incorrect QIR output record count")
        values = struct.unpack_from(f"<{length}I", memory, layout["data_address"])
        if any(value not in (0, 1) for value in values):
            raise AssertionError("QIR produced a non-bit output")
        bits = np.array([values[: reference.num_measurements]], dtype=np.bool_)
        detectors, logical = converter.convert(measurements=bits, separate_observables=True)
        predicted = int(oracle.decode(detectors[0])[0])
        raw = int(logical[0, 0])
        corrected = raw ^ predicted
        if values[-2:] != (raw, corrected):
            raise AssertionError("raw or corrected observable differs from the reference")
        if detectors.any() or corrected:
            raise AssertionError("ideal X-memory has a detector event or a logical error")
        events = [json.loads(line) for line in (directory / run["trace"]).read_text().splitlines()]
        timing = {}
        for kind in ("DecoderStarted", "DecoderCompleted", "DecoderResultReturned"):
            selected = [event for event in events if event["kind"] == kind]
            if len(selected) != 1:
                raise AssertionError(f"expected one {kind} event")
            timing[kind] = selected[0]["tick"]
        if timing["DecoderCompleted"] - timing["DecoderStarted"] != latency:
            raise AssertionError("decoder processing latency differs from the target")
        if timing["DecoderResultReturned"] < timing["DecoderCompleted"]:
            raise AssertionError("decoder result returned before processing completed")
        bitstring = "".join(map(str, values))
        counts[bitstring] += 1
        records.append(
            {
                "shot": shot,
                "seed": shot + 1,
                "raw_logical_result": raw,
                "decoder_flip": predicted,
                "corrected_logical_result": corrected,
                "exit_code": summary["exit_code"],
                "stop_tick": summary["stop_tick"],
                **timing,
            }
        )
    results = {
        "distance": 3,
        "qubits": 17,
        "measurements": reference.num_measurements,
        "detectors": reference.num_detectors,
        "backend": "stim",
        "shots": shots,
        "bit_order": "measurement records, raw observable, corrected observable",
        "counts": dict(sorted(counts.items())),
        "results": records,
    }
    (directory / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exporter", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--sim", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build-clang/bloq-qir"))
    parser.add_argument("--shots", type=int, default=4)
    parser.add_argument("--decoder-latency", type=int, default=1000)
    parser.add_argument("--qir-format", choices=("bc", "ll"), default="bc")
    args = parser.parse_args()
    if args.shots < 1 or args.shots > 0xFFFFFFFF or args.decoder_latency < 1:
        parser.error("shots must be in 1..4294967295 and decoder-latency must be positive")
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    reference = generate(directory, args.exporter.resolve(), args.decoder_latency)
    subprocess.run(
        [
            str(args.compiler.resolve()),
            str(directory / f"memory.{args.qir_format}"),
            "--target",
            str(directory / "target.json"),
            "-o",
            str(directory / "memory.elf"),
        ],
        check=True,
    )
    results = simulate(directory, args.sim.resolve(), args.shots, reference, args.decoder_latency)
    print(
        f"Validated {results['shots']} shots, distance 3, 17 qubits: {directory / 'results.json'}"
    )


if __name__ == "__main__":
    main()
