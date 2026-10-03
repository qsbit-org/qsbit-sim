"""Run the Figure 3 gate sequence with HISQ operations and compare scalar and dual-cw issue rates."""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def compile_program(directory, source, assembler, linker, **symbols):
    directory.mkdir(parents=True, exist_ok=True)
    examples = Path(__file__).resolve().parent.parent
    program = directory / "program.elf"
    subprocess.run([assembler, "-march=rv32i", "-mabi=ilp32", "-mno-relax", "-I", str(examples),
                    *[argument for key, value in symbols.items()
                      for argument in ("--defsym", f"{key}={value}")],
                    "-o", str(directory / "program.o"), str(source)], check=True)
    subprocess.run([linker, "-m", "elf32lriscv", "--no-relax", "-T", str(examples / "link.ld"),
                    "-o", str(program), str(directory / "program.o")], check=True)
    return program


def run_case(simulator, directory, settings, model, program):
    directory.mkdir(parents=True, exist_ok=True)
    config = {"schema": 1, "program": str(program.resolve()),
              "cpu_model": model, "backend": settings["backend"],
              "backend_options": settings["backend_options"],
              "profile": settings["profile"], "trace": "trace.jsonl", "summary": "summary.json"}
    path = directory / "run.json"
    path.write_text(json.dumps(config, indent=2) + "\n")
    result = subprocess.run([str(simulator), "--config", str(path)], capture_output=True,
                            text=True, timeout=60)
    (directory / "run.log").write_text(result.stdout + result.stderr)
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr)
    summary = json.loads((directory / "summary.json").read_text())
    trace = [json.loads(line) for line in (directory / "trace.jsonl").read_text().splitlines()]
    assert result.returncode == (0 if summary["success"] else 1)
    summary["program_sha256"] = hashlib.sha256(program.read_bytes()).hexdigest()
    return summary, trace


def figure3(simulator, output, settings, assembler, linker):
    source = Path(__file__).with_name("figure3.S")
    results = {}
    for mode in ("rv32", "vliw"):
        directory = output / ("figure3-" + mode)
        program = compile_program(directory, source, assembler, linker, VLIW=int(mode == "vliw"))
        summary, trace = run_case(simulator, directory, settings, mode, program)
        results[mode] = check_figure3(summary, trace, settings)
    assert results["rv32"]["operation_starts"] == results["vliw"]["operation_starts"]
    return results


def check_figure3(summary, trace, settings):
    assert summary["success"], summary
    starts = [e for e in trace if e["kind"] == "OperationStart"]
    period, origin = settings["profile"]["tcu"]["period"], settings["profile"]["start"]
    expected = [(origin + (10000 + i) * period, [q], operation)
                for i, operations in enumerate((("y", "y"), ("rx", "x"), ("measure", "measure")))
                for q, operation in zip((0, 2), operations)]
    observed = [(e["tick"], e["targets"], e["operation"]) for e in starts]
    assert observed == expected
    if settings["backend"] in ("aer", "stim"):
        assert not summary["measurement_registers"][2]["value"]
    return {"operation_starts": observed,
            "program_sha256": summary["program_sha256"]}


def check_issue(summary, trace, settings, points, interval_ns):
    starts = [event for event in trace if event["kind"] == "OperationStart"]
    expected = [(settings["profile"]["start"] + index * interval_ns, [q], operation)
                for index in range(points) for q, operation in ((0, "rx"), (2, "x"))]
    observed = [(event["tick"], event["targets"], event["operation"]) for event in starts]
    assert observed == expected[:len(observed)]
    if summary["success"]:
        assert observed == expected
        state = summary["statevector"]
        if state and points % 4 == 0:
            assert abs(sum(x * x for x in state[0]) - 1) < 1e-10
        if summary.get("density_matrix") and points % 4 == 0:
            assert abs(summary["density_matrix"][0][0][0] - 1) < 1e-10
    else:
        assert summary["fault"] == "LateAdmission", summary
    submitted = [e["tick"] for e in trace if e["kind"] == "TimingPointSubmitted"]
    assert len(submitted) >= 2
    elapsed = submitted[-1] - submitted[0]
    early = submitted[:8]
    retired = [e for e in trace if e["kind"] == "InstructionRetired"]
    return {"success": summary["success"], "fault": summary.get("fault"),
            "instructions_retired": len(retired),
            "last_retirement_tick_ns": retired[-1]["tick"],
            "timing_points_submitted": len(submitted),
            "submission_span_ns": elapsed,
            "mean_submission_interval_ns": elapsed / (len(submitted) - 1),
            "first_eight_mean_submission_interval_ns": (early[-1] - early[0]) / (len(early) - 1),
            "submission_span_cpu_cycles": elapsed / settings["profile"]["cpu"]["period"],
            "operations_started": len(starts), "stop_tick": summary["stop_tick"],
            "program_sha256": summary["program_sha256"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", type=Path, default=Path("build-clang/qsbit-sim"))
    parser.add_argument("--settings", type=Path, default=Path(__file__).with_name("experiment.json"))
    parser.add_argument("--output", type=Path, default=Path("build-clang/eqasm"))
    parser.add_argument("--points", type=int, default=256)
    parser.add_argument("--intervals", type=int, nargs="+", default=[20, 40, 60, 80, 100, 120],
                        help="gate start intervals in ns")
    parser.add_argument("--assembler", default=shutil.which("riscv64-unknown-elf-as") or shutil.which("riscv64-elf-as"))
    parser.add_argument("--linker", default=shutil.which("riscv64-unknown-elf-ld") or shutil.which("riscv64-elf-ld"))
    parser.add_argument("--backend", help="override the backend from the settings file")
    args = parser.parse_args()
    if not args.assembler or not args.linker:
        parser.error("RISC-V assembler and linker are required")
    settings = deepcopy(json.loads(args.settings.read_text()))
    if args.backend:
        settings["backend"] = args.backend
    period = settings["profile"]["tcu"]["period"]
    if not 32 <= args.points <= 4096:
        parser.error("--points must be between 32 and 4096")
    if any(n <= 0 or n % period or n // period > 131071 for n in args.intervals):
        parser.error("intervals must be positive multiples of the TCU period and fit wait.i")
    output, simulator = args.output.resolve(), args.simulator.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"settings": settings, "points": args.points,
              "simulator_sha256": hashlib.sha256(simulator.read_bytes()).hexdigest(),
              "figure3": figure3(simulator, output, settings, args.assembler, args.linker), "issue_rate": []}
    print("mode          interval_ns  completed  first_8_submission_ns  all_submission_ns")
    for mode in ("rv32", "vliw-scalar", "vliw-bundle"):
        for interval_ns in args.intervals:
            directory = output / f"{mode}-{interval_ns}ns"
            program = compile_program(directory, Path(__file__).with_name("issue_rate.S"),
                                      args.assembler, args.linker,
                                      VLIW=int(mode == "vliw-bundle"), POINTS=args.points,
                                      INTERVAL=interval_ns // period)
            summary, trace = run_case(simulator, directory, settings,
                                      "rv32" if mode == "rv32" else "vliw", program)
            metrics = check_issue(summary, trace, settings, args.points, interval_ns)
            report["issue_rate"].append(dict(metrics, mode=mode, interval_ns=interval_ns,
                                            program_words=args.points * (2 if mode == "vliw-bundle" else 3) + 3))
            print(f"{mode:13} {interval_ns:11}  {str(metrics['success']):9}  "
                  f"{metrics['first_eight_mean_submission_interval_ns']:21.2f}  "
                  f"{metrics['mean_submission_interval_ns']:.2f}")
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Results, ELF programs and traces: {output}")


if __name__ == "__main__":
    main()
