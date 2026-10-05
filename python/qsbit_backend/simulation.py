"""Execute or replay a fixed, symbol-delimited quantum program."""

import hashlib
import io
import json
import math
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from bisect import bisect_left
from copy import deepcopy
from importlib import metadata
from itertools import groupby
from pathlib import Path

from elftools.elf.elffile import ELFFile
from jsonschema import Draft202012Validator

from .registry import create, describe

SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["execution", "region", "repetition_count_symbol", "repetitions"],
    "properties": {
        "execution": {"enum": ["full", "replay"]},
        "quantum_execution": {"enum": ["direct", "transition_probabilities"]},
        "repetitions": {"type": "integer", "minimum": 1, "maximum": 4294967295},
        "repetition_count_symbol": {"type": "string", "minLength": 1},
        "region": {
            "type": "object",
            "additionalProperties": False,
            "required": ["begin", "end"],
            "properties": {"begin": {"type": "string"}, "end": {"type": "string"}},
        },
    },
}


def signed(value, bits):
    return (value ^ (1 << (bits - 1))) - (1 << (bits - 1))


def inspect_program(data, strategy):
    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 32 or not elf.little_endian or elf["e_machine"] != "EM_RISCV":
        raise ValueError("simulation requires a little-endian RV32 ELF")
    table = elf.get_section_by_name(".symtab")
    if table is None:
        raise ValueError("simulation requires an ELF symbol table")

    def symbol(name):
        matches = table.get_symbol_by_name(name) or []
        if len(matches) != 1 or not isinstance(matches[0]["st_shndx"], int):
            raise ValueError(f"missing or ambiguous ELF symbol: {name}")
        return matches[0]

    begin, end = [symbol(strategy["region"][key])["st_value"] for key in ("begin", "end")]
    counter = symbol(strategy["repetition_count_symbol"])
    section = elf.get_section(counter["st_shndx"])
    if not section["sh_flags"] & 1 or section["sh_type"] == "SHT_NOBITS":
        raise ValueError("repetition count must be a file-backed writable word")
    offset = section["sh_offset"] + counter["st_value"] - section["sh_addr"]
    if (
        counter["st_value"] % 4
        or not section["sh_offset"] <= offset <= section["sh_offset"] + section["sh_size"] - 4
    ):
        raise ValueError("invalid repetition count address")
    text = elf.get_section_by_name(".text")
    base = text["sh_addr"]
    code = text.data()
    if (
        elf["e_entry"] != base
        or begin != base + 12
        or end <= begin
        or end % 4
        or end + 20 != base + len(code)
    ):
        raise ValueError("program must use the documented fixed-repeat loop layout")
    words = struct.unpack("<" + "I" * (len(code) // 4), code)
    upper, lower, load = words[:3]
    if upper & 0xFFF != 0x297 or lower & 0xFFFFF != 0x28293 or load != 0x2A403:
        raise ValueError("loop prefix must load repetitions through t0 into s0")
    address = (base + (upper & 0xFFFFF000) + signed(lower >> 20, 12)) & 0xFFFFFFFF
    if address != counter["st_value"]:
        raise ValueError("loop does not load the configured repetition count symbol")
    tail = words[(end - base) // 4 :]
    branch = tail[1]
    displacement = signed(
        ((branch >> 31) << 12)
        | (((branch >> 7) & 1) << 11)
        | (((branch >> 25) & 63) << 5)
        | (((branch >> 8) & 15) << 1),
        13,
    )
    if (
        tail[0] != 0xFFF40413
        or branch & 0x1FFF07F != 0x41063
        or end + 4 + displacement != begin
        or list(tail[2:]) != [0x513, 0x5D00893, 0x73]
    ):
        raise ValueError("loop suffix must decrement s0, branch to begin and exit")
    body = words[3 : (end - base) // 4]
    for word in body:
        f3, f7, rd = (word >> 12) & 7, word >> 25, (word >> 7) & 31
        valid = (
            word & 127 == 11
            and rd == 0
            and (
                (f3 == 0 and f7 == 3) or f3 == 2 or (f3 == 3 and f7 == 0 and (word >> 20) & 31 == 0)
            )
        )
        if not valid:
            raise ValueError("repeat region accepts only cw.i.i, wait.i and fmr zero")
    if (body[-1] >> 12) & 7 != 3:
        raise ValueError("repeat region must end with fmr zero")
    return {"begin": begin, "end": end, "counter_offset": offset, "words": body}


def validate_mappings(program, profile):
    if profile["fast_feedback"]:
        raise ValueError("fixed-repeat simulation requires fast_feedback=false")
    mappings = {(m["port"], m["codeword"]): m["actions"] for m in profile["mappings"]}
    pending = set()
    interval = 0
    measurements = 0
    for word in program["words"]:
        f3 = (word >> 12) & 7
        if f3 == 2:
            if pending:
                raise ValueError("read all pending measurements before advancing time")
            interval += word >> 15
        elif f3 == 3:
            target = (word >> 15) & 31
            if target not in pending:
                raise ValueError("fmr must read a preceding acquisition")
            pending.remove(target)
        else:
            if pending:
                raise ValueError("read all pending measurements before another codeword")
            key = ((word >> 15) & 31, (word >> 20) & 31)
            if key not in mappings:
                raise ValueError(f"unmapped port and codeword: {key}")
            for action in mappings[key]:
                kind = action["kind"]
                if (
                    kind not in ("gate", "gate_output", "acquire")
                    or (kind == "gate" and action["execution_flag"] != "always")
                    or (kind == "acquire" and action["separate_arm"])
                ):
                    raise ValueError("replay supports unconditional gates and unarmed acquisitions")
                if action["kind"] == "acquire":
                    pending.update(action["targets"])
                    measurements += len(action["targets"])
    if pending or not measurements or interval <= 0:
        raise ValueError("repeat region must complete measurements and advance time")
    period = interval * profile["tcu"]["period"]
    if period % math.lcm(profile["cpu"]["period"], profile["tcu"]["period"]):
        raise ValueError("repeat duration must preserve CPU and TCU clock phases")
    return period, measurements


def normalized(calls, shift=0):
    result = deepcopy(calls)
    for call in result:
        call.pop("bits", None)
        call.pop("batch", None)
        call["tick"] -= shift
        if call["kind"] == "evolve":
            call["start"] -= shift
            for activity in [*call["drives"], *call["acquisitions"]]:
                activity["start"] -= shift
                activity["end"] -= shift
                activity.pop("id")
                activity.pop("measurement", None)
        elif call["kind"] == "measure":
            call["references"] = [{"target": r["target"]} for r in call["references"]]
    return result


def partition(calls, trace, program, rounds):
    ends = [
        e["tick"] for e in trace if e["kind"] == "InstructionRetired" and e["pc"] == program["end"]
    ]
    if len(ends) != rounds:
        raise ValueError("executed repetition count differs from configuration")
    groups = [[] for _ in ends]
    final_idle = None
    for call in calls:
        index = bisect_left(ends, call["tick"])
        if index == len(ends):
            if (
                call is not calls[-1]
                or call["kind"] != "evolve"
                or call["drives"]
                or trace[-1]["kind"] != "SimulationCompleted"
                or call["tick"] != trace[-1]["tick"]
            ):
                raise ValueError("device work remains after repeat-region end")
            final_idle = call
            continue
        groups[index].append(call)
    for event in trace:
        if event["kind"] == "OperationStart":
            index = bisect_left(ends, event["tick"])
            if index == len(ends) or event["tick"] + event["value"] > ends[index]:
                raise ValueError("operation crosses the repeat-region boundary")
    if not all(groups):
        raise ValueError("empty repeat region")
    return groups, final_idle


def run_config(path, executable, check_only=False):
    started = time.perf_counter()
    path = Path(path).resolve()
    config = json.loads(path.read_text())
    strategy = config.pop("simulation")
    Draft202012Validator(SCHEMA).validate(strategy)
    strategy["repetitions"] = int(strategy["repetitions"])
    strategy.setdefault("quantum_execution", "direct")
    if config.get("backend", "mock") == "mock":
        raise ValueError("repeated simulation requires a Python backend")
    if strategy["execution"] == "full" and strategy["quantum_execution"] != "direct":
        raise ValueError("full execution requires direct quantum execution")
    if (
        config.get("resets")
        or "raw_base" in config
        or config.get("memory_dump")
        or "trace" in config
    ):
        raise ValueError(
            "simulation strategies do not support resets, raw images, memory dumps or a trace override"
        )
    for key in ("program", "profile_file", "python_path", "summary", "trace"):
        if key in config:
            config[key] = str((path.parent / config[key]).resolve())
    if "summary" not in config:
        raise ValueError("simulation requires a summary output path")
    executable = shutil.which(executable) or str(Path(executable).resolve())
    previous_path = sys.path[:]
    try:
        if "python_path" in config:
            sys.path.insert(0, config["python_path"])
        return _execute(config, strategy, executable, check_only, started)
    finally:
        sys.path[:] = previous_path


def _execute(config, strategy, executable, check_only, started):
    source = Path(config["program"]).read_bytes()
    program = inspect_program(source, strategy)
    with tempfile.TemporaryDirectory(prefix="qsbit-simulation-") as directory:
        check_path = Path(directory) / "run.json"
        profile_path = Path(directory) / "profile.json"
        check_path.write_text(json.dumps(config))
        process = subprocess.run(
            [executable, "--config", str(check_path), "--dump-default-profile", str(profile_path)],
            capture_output=True,
            text=True,
        )
        if process.returncode:
            raise ValueError(process.stderr)
        resolved_profile = json.loads(profile_path.read_text())
        minimum_period, _ = validate_mappings(program, resolved_profile)
        if (
            resolved_profile["start"] + minimum_period * strategy["repetitions"]
            >= resolved_profile["watchdog"]
        ):
            raise ValueError("repeated time points reach the configured watchdog")
        if strategy["quantum_execution"] == "transition_probabilities" and (
            resolved_profile["qubits"] != 1
            or not describe(config["backend"])
            .get("capabilities", {})
            .get("transition_probabilities")
        ):
            raise ValueError("transition_probabilities requires a supporting single-qubit backend")
        if check_only:
            process = subprocess.run(
                [executable, "--config", str(check_path), "--check-config"],
                capture_output=True,
                text=True,
            )
            if process.returncode:
                raise ValueError(process.stderr)
            print("Configuration valid", flush=True)
            return
    output = Path(config["summary"])
    work = output.parent / (output.stem + "-control")
    work.mkdir(parents=True, exist_ok=True)
    name, options = config.get("backend", "mock"), config.get("backend_options", {})
    _, options = create(name, options)
    rounds = strategy["repetitions"] if strategy["execution"] == "full" else 3

    def control_run(label, outcome):
        image = bytearray(source)
        struct.pack_into("<I", image, program["counter_offset"], rounds)
        elf_path = work / (label + ".elf")
        elf_path.write_bytes(image)
        child = deepcopy(config)
        child.update(
            program=str(elf_path),
            backend="qsbit_backend.recording:RecordingBackend",
            backend_options={
                "backend": name,
                "options": options,
                "output": str(work / (label + "-calls.json")),
                "outcome": outcome,
            },
            trace_stalls=False,
            trace=str(work / (label + ".jsonl")),
            summary=str(work / (label + "-summary.json")),
        )
        child_path = work / (label + ".json")
        child_path.write_text(json.dumps(child, indent=2) + "\n")
        result = subprocess.run(
            [executable, "--config", str(child_path)], capture_output=True, text=True
        )
        if result.returncode:
            raise ValueError(f"control simulation failed: {result.stderr}\n{result.stdout}")
        summary = json.loads(Path(child["summary"]).read_text())
        calls = json.loads(Path(child["backend_options"]["output"]).read_text())
        trace = [json.loads(line) for line in Path(child["trace"]).read_text().splitlines()]
        return summary, calls, trace

    full = strategy["execution"] == "full"
    summary, calls, trace = control_run("full" if full else "zero", None if full else False)
    profile = summary["configuration"]
    period, measurements = validate_mappings(program, profile)
    if profile["start"] + period * strategy["repetitions"] > (1 << 64) - 1:
        raise ValueError("repeated simulation time exceeds uint64")
    if summary["stop_tick"] + (strategy["repetitions"] - rounds) * period > (1 << 64) - 1:
        raise ValueError("repeated controller completion exceeds uint64")
    if summary["stop_tick"] + (strategy["repetitions"] - rounds) * period >= profile["watchdog"]:
        raise ValueError("repeated controller completion reaches the configured watchdog")
    groups, final_idle = partition(calls, trace, program, rounds)
    if full:
        counts = [0] * measurements
        digest = hashlib.sha256()
        for group in groups:
            bits = [b for c in group if c["kind"] == "measure" for b in c["bits"]]
            if len(bits) != measurements:
                raise ValueError("measurement count differs from program")
            counts = [a + b for a, b in zip(counts, bits)]
            digest.update(bytes(bits))
        quantum = {
            "counts": counts,
            "probabilities": [c / rounds for c in counts],
            "measurement_sha256": digest.hexdigest(),
        }
    else:
        if normalized(groups[1], period) != normalized(groups[2], 2 * period):
            raise ValueError("quantum operation sequence is not periodic")
        one_summary, one_calls, one_trace = control_run("one", True)
        if (
            normalized(calls) != normalized(one_calls)
            or summary["stop_tick"] != one_summary["stop_tick"]
        ):
            raise ValueError("measurement outcomes change control timing")

        def control_signature(events):
            return [
                {k: v for k, v in e.items() if k not in ("value", "registers")}
                for e in events
                if e["kind"] in ("InstructionRetired", "TimingPointEnqueued", "OperationStart")
            ]

        if control_signature(trace) != control_signature(one_trace):
            raise ValueError("measurement outcomes change instruction or queue execution")
        if strategy["quantum_execution"] == "transition_probabilities":
            quantum = sample_transitions(name, options, profile, groups, strategy["repetitions"])
        else:
            quantum = replay_direct(
                name, options, profile, groups, period, strategy["repetitions"], final_idle
            )
    repetitions = strategy["repetitions"]
    acquisitions = {
        a["operation"] for m in profile["mappings"] for a in m["actions"] if a["kind"] == "acquire"
    }
    last_trigger = max(
        e["tick"] for e in trace if e["kind"] == "OperationStart" and e["operation"] in acquisitions
    )
    timing = {
        "period_ns": period,
        "period_tcu_cycles": period // profile["tcu"]["period"],
        "time_point_cycles": period // profile["tcu"]["period"] * repetitions,
        "time_point_ns": period * repetitions,
        "control_stop_tick_observed": summary["stop_tick"],
        "control_stop_tick": summary["stop_tick"] + (repetitions - rounds) * period,
        "control_stop_tick_source": "observed" if full else "extrapolated",
    }
    timing["last_measurement_trigger_ns"] = last_trigger + (repetitions - rounds) * period
    timing["last_measurement_sample_ns"] = (
        max(c["tick"] for c in calls if c["kind"] == "measure") + (repetitions - rounds) * period
    )
    result = {
        "schema": 1,
        "success": True,
        "simulation": strategy,
        "backend": name,
        "backend_execution": summary["backend_execution"],
        "backend_options": options,
        "profile": profile,
        "timing": timing,
        "control_repetitions_executed": rounds if full else 2 * rounds,
        "control_repetitions_per_validation_run": rounds,
        "measurement_count": measurements * repetitions,
        "program_sha256": hashlib.sha256(source).hexdigest(),
        "versions": {
            "python": platform.python_version(),
            **{
                package: metadata.version(package)
                for package in dict.fromkeys(
                    [
                        "qsbit-sim-backends",
                        *describe(name).get("requirements", []),
                        *(
                            ["numpy"]
                            if strategy["quantum_execution"] == "transition_probabilities"
                            else []
                        ),
                    ]
                )
            },
        },
        "host_seconds": time.perf_counter() - started,
        **quantum,
    }
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Completed {repetitions} repetitions; results: {output}", flush=True)


def replay_direct(name, options, profile, groups, period, repetitions, final_idle):
    backend, _ = create(name, options)
    backend.reset(profile["qubits"], profile["seed"])
    counts = None
    identity = 0
    digest = hashlib.sha256()
    for iteration in range(repetitions):
        group = groups[0] if iteration == 0 else groups[1]
        shift = 0 if iteration == 0 else (iteration - 1) * period
        identities = {}
        for call in group:
            if call["kind"] == "measure":
                for reference in call["references"]:
                    identity += 1
                    identities[reference["measurement"]] = identity
        bits = []
        for _, recorded in groupby(group, key=lambda call: call["batch"]):
            operations = deepcopy(list(recorded))
            expected_results = 0
            for operation in operations:
                operation.pop("batch")
                operation.pop("bits", None)
                operation["tick"] += shift
                if operation["kind"] == "evolve":
                    operation["start"] += shift
                    for activity in [*operation["drives"], *operation["acquisitions"]]:
                        activity["start"] += shift
                        activity["end"] += shift
                        if activity["kind"] == "acquire":
                            activity["measurement"] = identities[activity["measurement"]]
                if operation["kind"] == "measure":
                    expected_results = len(operation["references"])
                    for reference in operation["references"]:
                        reference.update(epoch=1, measurement=identities[reference["measurement"]])
            value = backend.execute(1, operations)
            if len(value) != expected_results:
                raise ValueError("backend returned the wrong measurement count")
            bits.extend(value)
        counts = [int(b) for b in bits] if counts is None else [c + b for c, b in zip(counts, bits)]
        digest.update(bytes(bits))
    if final_idle is not None:
        start, end, drives = final_idle["start"], final_idle["tick"], final_idle["drives"]
        shift = (repetitions - len(groups)) * period
        backend.execute(
            1,
            [
                {
                    "kind": "evolve",
                    "start": start + shift,
                    "tick": end + shift,
                    "drives": drives,
                    "acquisitions": [],
                }
            ],
        )
    return {
        "counts": counts,
        "probabilities": [c / repetitions for c in counts],
        "measurement_sha256": digest.hexdigest(),
    }


def sample_transitions(name, options, profile, groups, repetitions):
    import numpy as np

    backend, _ = create(name, options)
    if profile["qubits"] != 1 or not callable(getattr(backend, "transition_probabilities", None)):
        raise ValueError("transition_probabilities requires a supporting single-qubit backend")
    pending = []
    tables = []
    cache = {}
    for group in groups[:2]:
        table = []
        for call in group:
            if call["kind"] == "measure":
                if len(call["references"]) != 1 or call["references"][0]["target"] != 0:
                    raise ValueError(
                        "transition_probabilities requires single-qubit Z measurements"
                    )
                key = json.dumps(pending, sort_keys=True)
                if key not in cache:
                    cache[key] = backend.transition_probabilities(pending)
                table.append(cache[key])
                pending = []
            elif call["kind"] == "evolve":
                from_tick, to_tick, drives = call["start"], call["tick"], call["drives"]
                start = pending[-1]["tick"] if pending else 0
                duration = to_tick - from_tick
                pending.append(
                    {
                        "kind": "evolve",
                        "start": start,
                        "tick": start + duration,
                        "drives": drives,
                        "acquisitions": [],
                    }
                )
            else:
                pending.append(
                    {
                        "tick": pending[-1]["tick"] if pending else 0,
                        "kind": "apply",
                        "gates": call["gates"],
                    }
                )
        tables.append(table)
    rng = np.random.default_rng(profile["seed"])
    counts = [0] * len(tables[0])
    expected = [0.0] * len(counts)
    state = 0
    probability = 0.0
    for iteration in range(repetitions):
        table = tables[0] if iteration == 0 else tables[1]
        for index, (p0, p1) in enumerate(table):
            state = int(rng.random() < (p1 if state else p0))
            counts[index] += state
            probability = p0 + (p1 - p0) * probability
            expected[index] += probability
    return {
        "counts": counts,
        "probabilities": [c / repetitions for c in counts],
        "expected_probabilities": [p / repetitions for p in expected],
        "transition_probabilities": {"first": tables[0], "steady": tables[1]},
        "transition_calculations": len(cache),
        "last_measurement": state,
    }
