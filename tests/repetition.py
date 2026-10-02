"""Compare full execution, event replay and AllXY model expectations."""

import argparse
from copy import deepcopy
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser()
for option in ("simulator", "source", "build"):
    parser.add_argument("--" + option, type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.source / "python"))
from qsbit_backend.simulation import inspect_program, normalized, partition, sample_transitions

config = json.loads((args.source / "examples/allxy/run.json").read_text())
config["program"] = str((args.build / "examples/allxy.elf").resolve())
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

output = (args.build / "repetition-test").resolve()
output.mkdir(exist_ok=True)
precheck = deepcopy(config)
precheck["summary"] = str(output / "precheck-result.json")
precheck_path = output / "precheck-run.json"
precheck_path.write_text(json.dumps(precheck))
proc = subprocess.run([str(args.simulator), "--config", str(precheck_path), "--check-config"],
                      capture_output=True, text=True, timeout=30)
assert proc.returncode == 0, proc.stderr
assert not (output / "precheck-result.json").exists()
precheck["profile"]["watchdog"] = 100000000
precheck_path.write_text(json.dumps(precheck))
proc = subprocess.run([str(args.simulator), "--config", str(precheck_path), "--check-config"],
                      capture_output=True, text=True, timeout=30)
assert proc.returncode == 2 and "watchdog" in proc.stderr, proc.stderr
results = {}
for mode in ("full", "replay"):
    run = deepcopy(config)
    run["simulation"].update(execution=mode, quantum_execution="direct", repetitions=4)
    run["summary"] = str(output / f"{mode}.json")
    path = output / f"{mode}-run.json"
    path.write_text(json.dumps(run))
    proc = subprocess.run([str(args.simulator), "--config", str(path)], capture_output=True, text=True, timeout=240)
    assert proc.returncode == 0, proc.stderr + proc.stdout
    results[mode] = json.loads(Path(run["summary"]).read_text())
assert results["full"]["counts"] == results["replay"]["counts"]
assert results["full"]["measurement_sha256"] == results["replay"]["measurement_sha256"]
assert results["full"]["timing"]["control_stop_tick"] == results["replay"]["timing"]["control_stop_tick"]
assert results["full"]["timing"]["last_measurement_sample_ns"] == results["replay"]["timing"]["last_measurement_sample_ns"]
assert results["full"]["measurement_count"] == 168

work = output / "replay-control"
trace = [json.loads(line) for line in (work / "zero.jsonl").read_text().splitlines()]
starts = [event for event in trace if event["kind"] == "OperationStart"]
assert len(starts) == 3 * 42 * 3
for index, event in enumerate(starts):
    trial, operation = divmod(index, 3)
    assert event["tick"] == 1000 + trial * 200040 + 200000 + operation * 20
assert not any(event["kind"] == "CpuStalled" for event in trace)
calls = json.loads((work / "zero-calls.json").read_text())
groups = partition(calls, trace, program, 3)
assert normalized(groups[1], 8401680) == normalized(groups[2], 2 * 8401680)
result = results["replay"]
result.update(sample_transitions("aer", config["backend_options"], result["profile"], groups, 25600))
result["simulation"].update(repetitions=25600, quantum_execution="transition_probabilities")
result["measurement_count"] = 42 * 25600
result["timing"].update(time_point_ns=215083008000, last_measurement_trigger_ns=215083009000)
spec = importlib.util.spec_from_file_location("allxy_plot", args.source / "examples/allxy/plot.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.check_control(trace, calls, result["profile"]["start"])
module.check(result)
assert len(result["counts"]) == 42
print("PASS fixed-repeat rejection, full/replay counts and timing, and 25,600-shot AllXY model")
