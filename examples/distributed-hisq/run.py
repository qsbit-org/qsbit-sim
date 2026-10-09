"""Recreate neighbor timing and measure booking-based synchronization (BISP) overhead."""

import argparse
import hashlib
import json
import shutil
import subprocess
from copy import deepcopy
from pathlib import Path

HERE = Path(__file__).resolve().parent


def assemble(source, directory, assembler, linker, **symbols):
    directory.mkdir(parents=True, exist_ok=True)
    common = HERE.parent / "common"
    subprocess.run(
        [
            assembler,
            "-march=rv32i",
            "-mabi=ilp32",
            "-mno-relax",
            "-I",
            str(common),
            *[arg for key, value in symbols.items() for arg in ("--defsym", f"{key}={value}")],
            "-o",
            str(directory / "program.o"),
            str(source),
        ],
        check=True,
    )
    program = directory / "program.elf"
    subprocess.run(
        [
            linker,
            "-m",
            "elf32lriscv",
            "--no-relax",
            "-T",
            str(common / "link.ld"),
            "-o",
            str(program),
            str(directory / "program.o"),
        ],
        check=True,
    )
    return str(program.resolve())


def pulse(port, codeword, qubit, duration, delay=0):
    return {
        "port": port,
        "codeword": codeword,
        "actions": [
            {
                "kind": "pulse",
                "operation": "marker",
                "targets": [qubit],
                "duration": duration,
                "delay": delay,
                "amplitude": 0.0,
            }
        ],
    }


def execute(simulator, directory, config):
    directory.mkdir(parents=True, exist_ok=True)
    config = dict(config, schema=1, trace="trace.jsonl", summary="summary.json", trace_stalls=False)
    path = directory / "run.json"
    path.write_text(json.dumps(config, indent=2) + "\n")
    process = subprocess.run(
        [str(simulator), "--config", str(path)], capture_output=True, text=True, timeout=60
    )
    (directory / "run.log").write_text(process.stdout + process.stderr)
    if process.returncode:
        raise RuntimeError(f"{directory}: {process.stdout}\n{process.stderr}")
    summary = json.loads((directory / "summary.json").read_text())
    assert summary["success"], summary
    trace = [json.loads(line) for line in (directory / "trace.jsonl").read_text().splitlines()]
    return summary, trace


def signals(trace, core, kind):
    return [event for event in trace if event.get("core") == core and event["kind"] == kind]


def boards(args, settings):
    directory = args.output / "boards"
    programs = [
        assemble(
            HERE / f"{name}.S",
            directory / name,
            args.assembler,
            args.linker,
            REPETITIONS=settings["repetitions"],
        )
        for name in ("control", "readout")
    ]
    duration = settings["pulse_duration_ns"]
    mappings = [
        [
            pulse(21, 2, 0, duration),
            pulse(20, 2, 0, duration),
            pulse(7, 1, 0, duration, settings["control_output_delay_ns"]),
        ],
        [pulse(5, 1, 1, duration, settings["readout_output_delay_ns"])],
    ]
    config = {
        "backend": settings["backend"],
        "profile": settings["profile"],
        "sync_connections": settings["sync_connections"],
        "cores": [
            {"id": i + 1, "program": program, "profile": {"mappings": mappings[i]}}
            for i, program in enumerate(programs)
        ],
    }
    summary, trace = execute(args.simulator, directory, config)
    period = settings["profile"]["tcu"]["period"]
    origin = settings["profile"]["start"]
    (connection,) = settings["sync_connections"]
    assert (connection["first"], connection["second"]) == (1, 2)
    assert (connection["first_to_second"], connection["second_to_first"]) == (8, 6)
    assert settings["control_output_delay_ns"] == 57 * period + settings["readout_output_delay_ns"]
    expected_bookings = [[], []]
    expected_outputs = []
    control, readout = origin + period, origin
    for iteration in range(settings["repetitions"] * 3):
        control += (40, 80, 120)[iteration % 3] * period
        readout += 2 * period
        expected_bookings[0].append(control)
        expected_bookings[1].append(readout)
        release = max(control + 8 * period, readout + 6 * period)
        expected_outputs.append(release + settings["control_output_delay_ns"])
        control = release + 50 * period
        if iteration % 3 == 2:
            control += period
        readout = release + 57 * period
    for i, port in ((0, 7), (1, settings["profile"]["ports"] + 5)):
        assert [e["tick"] for e in signals(trace, i + 1, "SyncBooked")] == expected_bookings[i]
        observed = [e["tick"] for e in signals(trace, i + 1, "OperationStart") if e["port"] == port]
        assert observed == expected_outputs, (observed, expected_outputs)
    _, reverse = execute(
        args.simulator, directory / "reverse", dict(config, reverse_registration=True)
    )

    def key(e):
        return (e["tick"], e.get("core", -1), e["kind"], e["port"], e["id"], e["value"])

    kinds = {
        "OperationStart",
        "OperationEnd",
        "SyncBooked",
        "SyncCompleted",
        "TimerPaused",
        "TimerResumed",
    }
    assert sorted(map(key, (e for e in trace if e["kind"] in kinds))) == sorted(
        map(key, (e for e in reverse if e["kind"] in kinds))
    )
    return {
        "stop_tick": summary["stop_tick"],
        "sync_output_ticks_ns": expected_outputs,
        "output_skew_ns": [0] * len(expected_outputs),
        "program_sha256": [hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in programs],
    }


def booking_scan(args, settings):
    scan = settings["booking_scan"]
    profile = deepcopy(settings["profile"])
    period, origin = profile["tcu"]["period"], profile["start"]
    results = []
    for latency in scan["latencies"]:
        for duration in scan["durations"]:
            directory = args.output / f"booking-{latency}-{duration}"
            cores = []
            for i, ready in enumerate(scan["ready_cycles"]):
                program = assemble(
                    HERE / "booking.S",
                    directory / str(i + 1),
                    args.assembler,
                    args.linker,
                    READY=ready,
                    DURATION=duration,
                    LATENCY=latency,
                    PEER=2 - i,
                )
                cores.append(
                    {
                        "id": i + 1,
                        "program": program,
                        "profile": {
                            "mappings": [
                                pulse(0, 1, i, settings["pulse_duration_ns"]),
                                pulse(1, 1, i, settings["pulse_duration_ns"]),
                            ]
                        },
                    }
                )
            config = {
                "backend": settings["backend"],
                "profile": profile,
                "cores": cores,
                "sync_connections": [
                    {
                        "first": 1,
                        "second": 2,
                        "first_to_second": latency,
                        "second_to_first": latency,
                    }
                ],
            }
            _, trace = execute(args.simulator, directory, config)
            starts = [
                e["tick"]
                for e in trace
                if e["kind"] == "OperationStart" and e["port"] % profile["ports"] == 1
            ]
            expected = origin + (max(scan["ready_cycles"]) + max(duration, latency)) * period
            assert starts == [expected, expected], (starts, expected)
            overhead = (expected - origin) // period - max(scan["ready_cycles"]) - duration
            assert overhead == max(0, latency - duration)
            results.append(
                {
                    "latency_cycles": latency,
                    "duration_cycles": duration,
                    "overhead_cycles": overhead,
                    "output_skew_ns": 0,
                }
            )
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", default="qsbit-sim", help="simulator command or path")
    parser.add_argument("--settings", type=Path, default=HERE / "experiment.json")
    parser.add_argument("--output", type=Path, default=Path("build-clang/distributed-hisq"))
    parser.add_argument(
        "--assembler",
        default=shutil.which("riscv64-unknown-elf-as") or shutil.which("riscv64-elf-as"),
    )
    parser.add_argument(
        "--linker", default=shutil.which("riscv64-unknown-elf-ld") or shutil.which("riscv64-elf-ld")
    )
    args = parser.parse_args()
    if not args.assembler or not args.linker:
        parser.error("RISC-V assembler and linker are required")
    command = shutil.which(args.simulator)
    if command is None:
        parser.error("qsbit-sim not found; install it on PATH or pass --simulator PATH")
    args.output, args.simulator = args.output.resolve(), Path(command).resolve()
    settings = json.loads(args.settings.read_text())
    report = {
        "settings": settings,
        "simulator_sha256": hashlib.sha256(args.simulator.read_bytes()).hexdigest(),
        "boards": boards(args, settings),
        "booking_scan": booking_scan(args, settings),
    }
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"Verified {len(report['boards']['output_skew_ns'])} synchronized output pairs and "
        f"{len(report['booking_scan'])} booking configurations. Results: {args.output}"
    )


if __name__ == "__main__":
    main()
