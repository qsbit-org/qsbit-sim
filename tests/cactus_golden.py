"""Compare independently executed workloads with pinned CACTUS trace fixtures."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ALIASES = {"x180": "x", "z180": "z", "measz": "measure"}


def read_jsonl(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def normalize(events, reference, probes):
    output = []
    for event in events:
        kind = event["kind"]
        if kind == ("TcuOutput" if reference else "LabelFired") and "tcu" in probes:
            output.append(["tcu", event["tick"], event["label"]])
        elif kind == ("DeviceCommand" if reference else "OperationStart"):
            operation = event["operation"].lower()
            output.append(["operation", event["tick"], ALIASES.get(operation, operation), event["targets"]])
        elif kind in ("ResultReady", "CpuResultVisible") and kind in probes:
            target = event["target"] if reference else event["targets"][0]
            output.append([kind, event["tick"], target, int(event["value"])])
    return sorted(output, key=lambda event: (event[1], event[0], str(event[2:])))


def compare(expected, actual, case):
    for index, (left, right) in enumerate(zip(expected, actual)):
        if left != right:
            raise AssertionError(f"{case}: first divergent event {index}: expected {left}, got {right}")
    if len(expected) != len(actual):
        raise AssertionError(f"{case}: event count {len(expected)} != {len(actual)}; "
                             f"first extra event: {(expected if len(expected) > len(actual) else actual)[min(len(expected), len(actual))]}")


def run(command, log):
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=120)


def check_comparator():
    sample = [["tcu", 100, 1], ["operation", 140, "x", [0]]]
    mutations = [sample[:-1], sample + [sample[-1]],
                 [["tcu", 101, 1], sample[1]]]
    for mutation in mutations:
        try:
            compare(sample, mutation, "comparator check")
        except AssertionError:
            continue
        raise AssertionError("comparator accepted a missing, extra, or shifted event")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--simulator", type=Path, required=True)
    parser.add_argument("--assembler", type=Path, required=True)
    parser.add_argument("--linker", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    check_comparator()
    fixture = args.source / "tests/data/cactus"
    output = args.build / "cactus-golden"
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((fixture / "manifest.json").read_text())
    images = {}
    for case in manifest["cases"]:
        name = case["workload"]
        if name not in images:
            work = output / name
            work.mkdir(exist_ok=True)
            assembly = fixture / "workloads" / name / "program.S"
            obj, image = work / "program.o", work / "program.elf"
            run([str(args.assembler), "-march=rv32i", "-mabi=ilp32", "-mno-relax",
                 "-I", str(args.source / "examples"), "-o", str(obj), str(assembly)],
                work / "assemble.log")
            run([str(args.linker), "-m", "elf32lriscv", "--no-relax", "-T",
                 str(args.source / "examples/link.ld"), "-o", str(image), str(obj)],
                work / "link.log")
            images[name] = image
        case_name = f"{name}-{case['mode']}"
        case_output = output / case_name
        case_output.mkdir(exist_ok=True)
        probes = set(case["probes"])
        raw = fixture / "traces" / f"{case_name}.jsonl"
        if hashlib.sha256(raw.read_bytes()).hexdigest() != case["raw_sha256"]:
            raise AssertionError(f"{case_name}: raw reference trace hash changed")
        reference = read_jsonl(raw)
        if any(event["kind"] == "QueueError" for event in reference):
            raise AssertionError(f"{case_name}: reference queue error")
        golden = json.loads((fixture / "golden" / f"{case_name}.json").read_text())
        if len(golden) != case["event_count"]:
            raise AssertionError(f"{case_name}: golden event count changed")
        compare(golden, normalize(reference, True, probes), case_name + " fixture")
        profile = fixture / "profiles" / f"{case['profile']}.json"
        summary, trace = case_output / "summary.json", case_output / "native.jsonl"
        run([str(args.simulator), "--program", str(images[name]), "--profile", str(profile),
             "--backend", "aer", "--trace", str(trace), "--summary", str(summary)],
            case_output / "native.log")
        if not json.loads(summary.read_text())["success"]:
            raise AssertionError(f"{case_name}: native simulation failed")
        compare(golden, normalize(read_jsonl(trace), False, probes), case_name)
        print(f"PASS {case_name}: {len(golden)} events", flush=True)


if __name__ == "__main__":
    main()
