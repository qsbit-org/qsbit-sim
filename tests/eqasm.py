"""Check eQASM execution through the shared memory and timing controller."""

import argparse
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--simulator", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
program = args.output / "program.bin"
program.write_bytes(struct.pack("<9I", 0x40000001, 0x40100002, 0x40200003, 0x60000008,
                                0x80400308, 0x81040001, 0x2a000000, 0x2a100001, 0x10000000))
microcode = [dict(opcode=opcode, targets=[q], port=q, codeword=opcode)
             for opcode, q in ((1, 0), (3, 1), (4, 0), (4, 1))]


def run(name, fault=None, **overrides):
    config = {"schema": 1, "program": str(program.resolve()), "raw_base": 0,
              "cpu_model": "eqasm", "eqasm": {"microcode": microcode},
              "outcomes": [False, True], "trace": name + ".jsonl", "summary": name + ".json",
              **overrides}
    path = args.output / (name + "-run.json")
    path.write_text(json.dumps(config))
    result = subprocess.run([str(args.simulator), "--config", str(path)], capture_output=True,
                            text=True, timeout=15)
    if fault:
        assert result.returncode == 2 and fault in result.stderr, result.stderr
        return
    assert result.returncode == 0, result.stderr + result.stdout
    summary = json.loads((args.output / (name + ".json")).read_text())
    trace = [json.loads(line) for line in (args.output / (name + ".jsonl")).read_text().splitlines()]
    assert summary["success"] and summary["registers"][:2] == [0, 1]
    assert summary["pc"] == 32 and summary["cpu_model"] == "eqasm"
    return summary, trace


for name, overrides, origin in (
        ("normal", {}, 1000),
        ("slow_memory", {"profile": {"memory_latency": 7}}, 1000),
        ("clock_phases", {"profile": {"cpu": {"period": 7, "phase": 2},
                                      "tcu": {"period": 23, "phase": 3}, "start": 1015}}, 1015),
        ("reset_fetch", {"resets": [15]}, 1020),
        ("reset_wait", {"resets": [100]}, 1100),
        ("reset_device", {"resets": [1165]}, 2180)):
    summary, trace = run(name, **overrides)
    reverse, reverse_trace = run(name + "-reverse", reverse_registration=True, **overrides)
    assert summary == reverse and trace == reverse_trace
    epoch = 2 if overrides.get("resets") else 1
    period = summary["configuration"]["tcu"]["period"]
    starts = [e for e in trace if e["kind"] == "OperationStart" and e["epoch"] == epoch]
    assert [(e["tick"], e["targets"], e["operation"]) for e in starts] == [
        (origin + 8 * period, [0], "x"), (origin + 8 * period, [1], "z"),
        (origin + 9 * period, [0], "measure"), (origin + 9 * period, [1], "measure")]
    updated = [e for e in trace if e["kind"] == "MeasurementRegisterUpdated" and e["epoch"] == epoch]
    read = [e for e in trace if e["kind"] == "MeasurementRegisterRead" and e["epoch"] == epoch]
    assert len(read) == 2 and len(updated) == 2
    assert all(r["tick"] >= u["tick"] for r, u in zip(read, updated))

for name, value in (("unknown_model", {"cpu_model": "unknown"}),
                    ("rv32_with_microcode", {"cpu_model": "rv32"}),
                    ("negative_opcode", {"eqasm": {"microcode": [dict(microcode[0], opcode=-1)]}}),
                    ("unknown_option", {"eqasm": {"microcode": microcode, "typo": 1}})):
    run(name, fault="InvalidProfile", **value)
