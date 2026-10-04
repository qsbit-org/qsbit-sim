"""Measure Aer execution of explicit batches of device operations."""

import argparse
import json
import platform
import statistics
import sys
import time
from importlib.metadata import version
from pathlib import Path
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from qsbit_backend.aer import AerBackend


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qubits", type=int, default=8)
    parser.add_argument("--operations", type=int, default=4096)
    parser.add_argument(
        "--measure-every",
        type=int,
        default=256,
        help="Gate events between joint measurements; zero measures only at the end.",
    )
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--batch-sizes", type=int, nargs="+", default=[1, 1024])
    parser.add_argument("--backend-options", type=Path, help="JSON object of Aer backend options.")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if (
        args.qubits < 2
        or args.operations < 1
        or args.measure_every < 0
        or args.repeats < 1
        or min(args.batch_sizes) < 1
    ):
        parser.error(
            "qubits must be at least two; counts must be positive and measure-every nonnegative"
        )
    options = json.loads(args.backend_options.read_text()) if args.backend_options else {}
    operations = []
    for index in range(args.operations):
        operation = ("h", "s", "cx")[index % 3]
        target = (index // 3) % args.qubits
        targets = [target, (target + 1) % args.qubits] if operation == "cx" else [target]
        operations.append(
            {"kind": "gate", "operation": operation, "targets": targets, "amplitude": 0}
        )
    references = [{"epoch": 1, "measurement": q, "target": q} for q in range(args.qubits)]
    segments = []
    interval = args.measure_every or args.operations
    for begin in range(0, args.operations, interval):
        end = min(begin + interval, args.operations)
        segment = []
        for tick in range(begin, end):
            segment.extend(
                [
                    {"tick": tick + 1, "kind": "evolve", "start": tick, "drives": []},
                    {"tick": tick + 1, "kind": "apply", "gates": [operations[tick]]},
                ]
            )
        segment.append({"tick": end, "kind": "measure", "references": references})
        segments.append(segment)
    records = []
    expected = None
    for size in args.batch_sizes:
        batches = [
            segment[begin : begin + size]
            for segment in segments
            for begin in range(0, len(segment), size)
        ]
        backend = AerBackend(options)
        backend.reset(args.qubits, 1)
        backend.execute(1, [{"tick": 0, "kind": "apply", "gates": operations[:1]}])
        backend.state()
        backend.density_matrix()
        elapsed, job_counts = [], []
        original_run = backend.executor.simulator.run
        for _ in range(args.repeats):
            backend.reset(args.qubits, 123)
            jobs = 0

            def counted_run(*positional, **keywords):
                nonlocal jobs
                jobs += 1
                return original_run(*positional, **keywords)

            bits = []
            with patch.object(backend.executor.simulator, "run", counted_run):
                start = time.perf_counter()
                for batch in batches:
                    bits.extend(backend.execute(1, batch))
                state = (
                    backend.state()
                    if backend.options["method"] == "statevector"
                    else backend.density_matrix()
                )
                elapsed.append(time.perf_counter() - start)
                job_counts.append(jobs)
            if expected is None:
                expected = bits, state
            if bits != expected[0]:
                raise AssertionError(
                    "measurement samples differ between batch sizes or repetitions"
                )
            np.testing.assert_allclose(state, expected[1], atol=1e-10, rtol=1e-10)
        records.append(
            {
                "backend_options": backend.options,
                "max_batch_operations": size,
                "backend_calls": len(batches),
                "seconds": elapsed,
                "median_seconds": statistics.median(elapsed),
                "aer_jobs": job_counts,
            }
        )
    report = {
        "qubits": args.qubits,
        "gate_events": args.operations,
        "measure_every": args.measure_every,
        "repeats": args.repeats,
        "platform": platform.platform(),
        "python": platform.python_version(),
        "versions": {name: version(name) for name in ("qiskit", "qiskit-aer", "numpy")},
        "results": records,
    }
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
