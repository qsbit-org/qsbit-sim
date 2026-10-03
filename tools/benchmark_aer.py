"""Measure Aer batching on a stream of single-gate device events."""

import argparse
from importlib.metadata import version
import json
from pathlib import Path
import platform
import statistics
import sys
import time
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from qsbit_backend.aer import AerBackend


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qubits", type=int, default=8)
    parser.add_argument("--operations", type=int, default=4096)
    parser.add_argument("--measure-every", type=int, default=256,
                        help="Gate events between joint measurements; zero measures only at the end.")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--batch-sizes", type=int, nargs="+", default=[1, 1024])
    parser.add_argument("--backend-options", type=Path, help="JSON object of Aer backend options.")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if (args.qubits < 2 or args.operations < 1 or args.measure_every < 0
            or args.repeats < 1 or min(args.batch_sizes) < 1):
        parser.error("qubits must be at least two; counts must be positive and measure-every nonnegative")
    options = json.loads(args.backend_options.read_text()) if args.backend_options else {}
    operations = []
    for index in range(args.operations):
        operation = ("h", "s", "cx")[index % 3]
        target = (index // 3) % args.qubits
        targets = [target, (target + 1) % args.qubits] if operation == "cx" else [target]
        operations.append({"kind": "gate", "operation": operation, "targets": targets, "amplitude": 0})
    references = [{"target": q} for q in range(args.qubits)]
    records = []
    expected = None
    for size in args.batch_sizes:
        backend = AerBackend(dict(options, max_batch_operations=size))
        backend.reset(args.qubits, 1)
        backend.apply(operations[:1])
        backend.state()
        backend.density_matrix()
        elapsed, job_counts = [], []
        original_run = backend._simulator.run
        for _ in range(args.repeats):
            backend.reset(args.qubits, 123)
            jobs = 0

            def counted_run(*positional, **keywords):
                nonlocal jobs
                jobs += 1
                return original_run(*positional, **keywords)

            bits = []
            with patch.object(backend._simulator, "run", counted_run):
                start = time.perf_counter()
                for tick, event in enumerate(operations):
                    backend.evolve(tick, tick + 1, [])
                    backend.apply([event])
                    if args.measure_every and (tick + 1) % args.measure_every == 0:
                        bits.extend(backend.measure(references))
                if not args.measure_every or args.operations % args.measure_every:
                    bits.extend(backend.measure(references))
                state = backend.state() if backend.options["method"] == "statevector" else backend.density_matrix()
                elapsed.append(time.perf_counter() - start)
                job_counts.append(jobs)
            if expected is None:
                expected = bits, state
            if bits != expected[0]:
                raise AssertionError("measurement samples differ between batch sizes or repetitions")
            np.testing.assert_allclose(state, expected[1], atol=1e-10, rtol=1e-10)
        records.append({"backend_options": backend.options, "seconds": elapsed,
                        "median_seconds": statistics.median(elapsed), "aer_jobs": job_counts})
    report = {"qubits": args.qubits, "gate_events": args.operations,
              "measure_every": args.measure_every, "repeats": args.repeats,
              "platform": platform.platform(), "python": platform.python_version(),
              "versions": {name: version(name) for name in ("qiskit", "qiskit-aer", "numpy")},
              "results": records}
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
