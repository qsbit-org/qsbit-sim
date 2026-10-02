"""Check repeat-region validation and persistent-state replay."""

import argparse
from copy import deepcopy
import io
import json
from pathlib import Path
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser()
for option in ("simulator", "source", "build", "assembler", "linker"):
    parser.add_argument("--" + option, type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.source / "python"))
from qsbit_backend.simulation import inspect_program

output = (args.build / "repetition-test").resolve()
output.mkdir(exist_ok=True)
subprocess.run([str(args.assembler), "-march=rv32i", "-mabi=ilp32", "-I", str(args.source / "examples"),
                str(args.source / "tests/repetition.S"), "-o", str(output / "repeat.o")], check=True)
subprocess.run([str(args.linker), "-m", "elf32lriscv", "-T", str(args.source / "examples/link.ld"),
                str(output / "repeat.o"), "-o", str(output / "repeat.elf")], check=True)
config = {
    "schema": 1, "program": str(output / "repeat.elf"), "backend": "aer",
    "simulation": {"execution": "replay", "quantum_execution": "direct", "repetitions": 4,
                   "region": {"begin": "repeat_begin", "end": "repeat_end"},
                   "repetition_count_symbol": "repetitions"},
    "profile": {"qubits": 1, "ports": 1, "start": 1000, "watchdog": 100000,
                "fast_feedback": False,
                "mappings": [
                    {"port": 0, "codeword": 1, "actions": [
                        {"operation": "x", "targets": [0], "duration": 20}]},
                    {"port": 0, "codeword": 2, "actions": [
                        {"kind": "acquire", "operation": "measure", "targets": [0],
                         "duration": 100, "discriminator_delay": 20}]}]},
}
data = Path(config["program"]).read_bytes()
program = inspect_program(data, config["simulation"])
text_offset = ELFFile(io.BytesIO(data)).get_section_by_name(".text")["sh_offset"]
for invalid in (0x00000063, 0x00100513, 0x0000308b):
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
precheck_path.write_text(json.dumps(precheck))
proc = subprocess.run([str(args.simulator), "--config", str(precheck_path), "--check-config"],
                      capture_output=True, text=True, timeout=30)
assert proc.returncode == 0, proc.stderr
assert not (output / "precheck-result.json").exists()
precheck["profile"]["watchdog"] = 2000
precheck_path.write_text(json.dumps(precheck))
proc = subprocess.run([str(args.simulator), "--config", str(precheck_path), "--check-config"],
                      capture_output=True, text=True, timeout=30)
assert proc.returncode == 2 and "watchdog" in proc.stderr, proc.stderr
results = {}
for mode in ("full", "replay", "transition_probabilities"):
    run = deepcopy(config)
    run["simulation"].update(execution="full" if mode == "full" else "replay",
                             quantum_execution="transition_probabilities" if mode == "transition_probabilities" else "direct")
    run["summary"] = str(output / f"{mode}.json")
    path = output / f"{mode}-run.json"
    path.write_text(json.dumps(run))
    proc = subprocess.run([str(args.simulator), "--config", str(path)], capture_output=True, text=True, timeout=240)
    assert proc.returncode == 0, proc.stderr + proc.stdout
    results[mode] = json.loads(Path(run["summary"]).read_text())
for result in results.values():
    assert result["success"] and result["measurement_count"] == 4
    assert result["counts"] == [2]
assert results["transition_probabilities"]["transition_probabilities"] == {
    "first": [[1.0, 0.0]], "steady": [[1.0, 0.0]]}
assert results["full"]["measurement_sha256"] == results["replay"]["measurement_sha256"]
assert results["full"]["timing"]["control_stop_tick"] == results["replay"]["timing"]["control_stop_tick"]
assert results["full"]["timing"]["last_measurement_sample_ns"] == results["replay"]["timing"]["last_measurement_sample_ns"]
print("PASS repeat-region validation, persistent-state replay and completion timing")
