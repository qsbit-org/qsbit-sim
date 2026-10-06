"""Check repeat-region validation and persistent-state replay."""

import argparse
import hashlib
import io
import json
import struct
import subprocess
import sys
from copy import deepcopy
from pathlib import Path

from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser()
for option in ("simulator", "source", "build", "assembler", "linker"):
    parser.add_argument("--" + option, type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.source / "python"))
from qsbit_backend.simulation import inspect_program, run_config  # noqa: E402

output = (args.build / "repetition-test").resolve()
output.mkdir(exist_ok=True)
subprocess.run(
    [
        str(args.assembler),
        "-march=rv32i",
        "-mabi=ilp32",
        "-I",
        str(args.source / "examples/common"),
        str(args.source / "tests/repetition.S"),
        "-o",
        str(output / "repeat.o"),
    ],
    check=True,
)
subprocess.run(
    [
        str(args.linker),
        "-m",
        "elf32lriscv",
        "-T",
        str(args.source / "examples/common/link.ld"),
        str(output / "repeat.o"),
        "-o",
        str(output / "repeat.elf"),
    ],
    check=True,
)
config = {
    "schema": 1,
    "program": str(output / "repeat.elf"),
    "backend": "aer",
    "simulation": {
        "execution": "replay",
        "quantum_execution": "direct",
        "repetitions": 4,
        "region": {"begin": "repeat_begin", "end": "repeat_end"},
        "repetition_count_symbol": "repetitions",
    },
    "profile": {
        "qubits": 1,
        "ports": 1,
        "start": 1000,
        "watchdog": 100000,
        "fast_feedback": False,
        "two_qubit_gates": [],
        "mappings": [
            {
                "port": 0,
                "codeword": 1,
                "actions": [{"operation": "x", "targets": [0], "duration": 20}],
            },
            {
                "port": 0,
                "codeword": 2,
                "actions": [
                    {
                        "kind": "acquire",
                        "operation": "measure",
                        "targets": [0],
                        "duration": 100,
                        "discriminator_delay": 20,
                    }
                ],
            },
        ],
    },
}
data = Path(config["program"]).read_bytes()
program = inspect_program(data, config["simulation"])
text_offset = ELFFile(io.BytesIO(data)).get_section_by_name(".text")["sh_offset"]
for invalid in (0x00000063, 0x00100513, 0x0000308B):
    changed = bytearray(data)
    struct.pack_into("<I", changed, text_offset + program["begin"], invalid)
    try:
        inspect_program(changed, config["simulation"])
        raise AssertionError("accepted a branch, classical instruction or feedback register")
    except ValueError:
        pass
bad = deepcopy(config["simulation"])
bad["region"]["begin"] = "missing_label"
try:
    inspect_program(data, bad)
    raise AssertionError("accepted a missing label")
except ValueError:
    pass

precheck = deepcopy(config)
precheck["summary"] = str(output / "precheck-result.json")
precheck_path = output / "precheck-run.json"
precheck_path.write_text(json.dumps(dict(precheck, decoding={})))
try:
    run_config(precheck_path, str(args.simulator), check_only=True)
    raise AssertionError("accepted decoder feedback in a repetition strategy")
except ValueError as error:
    assert "decoder feedback" in str(error), str(error)
precheck_path.write_text(json.dumps(precheck))
proc = subprocess.run(
    [str(args.simulator), "--config", str(precheck_path), "--check-config"],
    capture_output=True,
    text=True,
    timeout=30,
)
assert proc.returncode == 0, proc.stderr
assert not (output / "precheck-result.json").exists()
scoped = deepcopy(precheck)
scoped["backend"] = "custom_backend:Backend"
scoped["python_path"] = str((args.source / "tests/fixtures").resolve())
scoped_path = output / "scoped-run.json"
scoped_path.write_text(json.dumps(scoped))
original_path = sys.path[:]
run_config(scoped_path, str(args.simulator), check_only=True)
assert sys.path == original_path
scoped["program"] = str(output / "missing.elf")
scoped_path.write_text(json.dumps(scoped))
for _ in range(3):
    try:
        run_config(scoped_path, str(args.simulator), check_only=True)
        raise AssertionError("accepted a missing program")
    except FileNotFoundError:
        pass
    assert sys.path == original_path
precheck["profile"]["watchdog"] = 2000
precheck_path.write_text(json.dumps(precheck))
proc = subprocess.run(
    [str(args.simulator), "--config", str(precheck_path), "--check-config"],
    capture_output=True,
    text=True,
    timeout=30,
)
assert proc.returncode == 2 and "watchdog" in proc.stderr, proc.stderr


def execute(run, name):
    run["summary"] = str(output / f"{name}.json")
    path = output / f"{name}-run.json"
    path.write_text(json.dumps(run))
    proc = subprocess.run(
        [str(args.simulator), "--config", str(path)], capture_output=True, text=True, timeout=240
    )
    assert proc.returncode == 0, proc.stderr + proc.stdout
    return json.loads(Path(run["summary"]).read_text())


results = {}
for mode in ("full", "replay", "transition_probabilities"):
    run = deepcopy(config)
    run["simulation"].update(
        execution="full" if mode == "full" else "replay",
        quantum_execution="transition_probabilities"
        if mode == "transition_probabilities"
        else "direct",
    )
    results[mode] = execute(run, mode)
for result in results.values():
    assert result["success"] and result["measurement_count"] == 4
    assert result["counts"] == [2]
assert results["transition_probabilities"]["transition_probabilities"] == {
    "first": [[1.0, 0.0]],
    "steady": [[1.0, 0.0]],
}
assert results["full"]["measurement_sha256"] == results["replay"]["measurement_sha256"]
assert (
    results["full"]["timing"]["control_stop_tick"]
    == results["replay"]["timing"]["control_stop_tick"]
)
assert (
    results["full"]["timing"]["last_measurement_sample_ns"]
    == results["replay"]["timing"]["last_measurement_sample_ns"]
)

subprocess.run(
    [
        str(args.assembler),
        "-march=rv32i",
        "-mabi=ilp32",
        "--defsym",
        "PAIRED_GATES=1",
        "-I",
        str(args.source / "examples/common"),
        str(args.source / "tests/repetition.S"),
        "-o",
        str(output / "paired.o"),
    ],
    check=True,
)
subprocess.run(
    [
        str(args.linker),
        "-m",
        "elf32lriscv",
        "-T",
        str(args.source / "examples/common/link.ld"),
        str(output / "paired.o"),
        "-o",
        str(output / "paired.elf"),
    ],
    check=True,
)
paired = deepcopy(config)
paired["program"] = str(output / "paired.elf")
profile = paired["profile"]
profile.update(
    qubits=2,
    ports=2,
    two_qubit_gates=[
        {
            "name": "cx01",
            "operation": "cx",
            "targets": [0, 1],
            "duration": 20,
            "inputs": [
                {"core": 0, "port": 0, "codeword": 6},
                {"core": 0, "port": 1, "codeword": 10},
            ],
        }
    ],
)
profile["mappings"][1]["actions"][0]["targets"] = [1]
profile["mappings"].extend(
    [
        {"port": port, "codeword": code, "actions": [{"kind": "gate_output", "gate": "cx01"}]}
        for port, code in ((0, 6), (1, 10))
    ]
)
for mode in ("full", "replay"):
    run = deepcopy(paired)
    run["simulation"]["execution"] = mode
    result = execute(run, "paired-" + mode)
    assert result["success"] and result["measurement_count"] == 4
    assert result["measurement_sha256"] == hashlib.sha256(bytes([1, 1, 0, 0])).hexdigest()
print("PASS repeat-region validation, persistent-state replay and completion timing")
