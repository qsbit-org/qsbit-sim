"""Check dual-cw execution, scalar dependencies and shared timing control."""

import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for key in ("simulator", "assembler", "linker", "source", "output"):
    parser.add_argument("--" + key, type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def compile_case(name, body, packed=True):
    source = args.output / (name + ".S")
    source.write_text('.include "quantum.inc"\n.global _start\n.text\n_start:\n' + body + "\n")
    obj = source.with_suffix(".o")
    program = source.with_suffix(".elf")
    subprocess.run([str(args.assembler), "-march=rv32i", "-mabi=ilp32", "-mno-relax",
                    "-I", str(args.source / "examples"), "--defsym", f"VLIW={int(packed)}",
                    "-o", str(obj), str(source)], check=True, capture_output=True)
    subprocess.run([str(args.linker), "-m", "elf32lriscv", "--no-relax",
                    "-T", str(args.source / "examples/link.ld"), "-o", str(program), str(obj)],
                   check=True, capture_output=True)
    return program


def run(name, program, fault=None, **overrides):
    config = {"schema": 1, "program": str(program.resolve()), "cpu_model": "vliw",
              "outcomes": [False, True], "trace": name + ".jsonl", "summary": name + ".json",
              **overrides}
    path = args.output / (name + "-run.json")
    path.write_text(json.dumps(config))
    result = subprocess.run([str(args.simulator), "--config", str(path)], capture_output=True,
                            text=True, timeout=15)
    assert result.returncode == (1 if fault else 0), result.stderr + result.stdout
    summary = json.loads((args.output / (name + ".json")).read_text())
    trace = [json.loads(line) for line in (args.output / (name + ".jsonl")).read_text().splitlines()]
    assert summary["success"] == (fault is None)
    if fault:
        assert summary["fault"] == fault, summary
    return summary, trace


body = """
li t0, 4096
li t1, 3
sw t1, 0(t0)
lw t2, 0(t0)
addi t3, t2, -2
beq t1, t2, taken
cw.bundle 0, 31, 1, 31
taken:
wait.i 8
.if VLIW
  cw.bundle 0, 28, 28, 7, 1, 0
.else
  cw.i.r 0, t3
  cw.r.r t3, t2
.endif
wait.i 1
.if VLIW
  cw.bundle 0, 4, 1, 4
.else
  cw.i.i 0, 4
  cw.i.i 1, 4
.endif
fmr s0, 0
fmr s1, 1
add s2, s0, s1
sim_exit
"""
packed = compile_case("packed", body)
scalar = compile_case("scalar", body, packed=False)

for name, overrides, origin in (
        ("normal", {}, 1000),
        ("slow_memory", {"profile": {"memory_latency": 7, "start": 4000}}, 4000),
        ("clock_phases", {"profile": {"cpu": {"period": 7, "phase": 2},
                                     "tcu": {"period": 23, "phase": 3}, "start": 1015}}, 1015),
        ("reset_fetch", {"resets": [15]}, 1020),
        ("reset_device", {"resets": [1165]}, 2180)):
    summary, trace = run(name, packed, **overrides)
    reverse, reverse_trace = run(name + "-reverse", packed, reverse_registration=True, **overrides)
    assert summary == reverse and trace == reverse_trace
    assert summary["registers"][8:10] == [0, 1] and summary["registers"][18] == 1
    epoch = 2 if overrides.get("resets") else 1
    period = summary["configuration"]["tcu"]["period"]
    starts = [e for e in trace if e["kind"] == "OperationStart" and e["epoch"] == epoch]
    observed = [(e["tick"], e["targets"], e["operation"]) for e in starts]
    assert observed == [
        (origin + 8 * period, [0], "x"), (origin + 8 * period, [1], "z"),
        (origin + 9 * period, [0], "measure"), (origin + 9 * period, [1], "measure")]
    if name == "normal":
        queued = [e for e in trace if e["kind"] == "CodewordQueued"]
        assert queued[0]["tick"] == queued[1]["tick"]
        assert queued[2]["tick"] == queued[3]["tick"]
        retired = [e for e in trace if e["kind"] == "InstructionRetired"]
        assert sum((e["word"] & 127) == 0x2b for e in retired) == 2
        for model in ("rv32", "vliw"):
            baseline, baseline_trace = run("scalar-" + model, scalar, cpu_model=model)
            assert baseline["registers"] == summary["registers"]
            assert [(e["tick"], e["targets"], e["operation"]) for e in baseline_trace
                    if e["kind"] == "OperationStart"] == observed

conflict = compile_case("conflict", "cw.bundle 0, 6, 1, 1\nsim_exit")
run("conflict", conflict, fault="ResourceConflict")
run("rv32-rejects-bundle", packed, cpu_model="rv32", fault="IllegalInstruction")
invalid = compile_case("invalid", ".word 0x8000002b\nsim_exit")
_, trace = run("reserved-bit", invalid, fault="IllegalInstruction")
assert not any(e["kind"] == "CodewordQueued" for e in trace)
