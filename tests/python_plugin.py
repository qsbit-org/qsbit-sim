"""Exercise an external backend without importing installed numerical packages."""

import argparse
import importlib
import json
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser()
for name in ("simulator", "source", "build"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
sys.path.insert(0, str(a.source / "python"))
importlib.import_module("qsbit_backend")
assert all(
    name not in sys.modules
    for name in (
        "qsbit_backend.aer",
        "qsbit_backend.qutip",
        "qiskit",
        "qiskit_aer",
        "scipy",
        "numpy",
    )
)
out = a.build / "plugin-test"
out.mkdir(exist_ok=True)
command = [
    str(a.simulator),
    "--program",
    str(a.build / "examples/measurement-feedback/program.elf"),
    "--python-path",
    str(a.source / "tests/fixtures"),
    "--trace",
    str(out / "trace.jsonl"),
    "--summary",
    str(out / "summary.json"),
    "--inspect",
    "4096",
]
result = subprocess.run(
    command + ["--backend", "custom_backend:Backend"], capture_output=True, text=True, timeout=20
)
assert result.returncode == 0, result.stderr
summary = json.loads((out / "summary.json").read_text())
assert summary["success"] and summary["memory"]["4096"] == 1, summary
assert summary["statevector"] == [], summary
config = out / "configured.json"
config.write_text(
    json.dumps(
        {"schema": 1, "backend": "custom_backend:Configured", "backend_options": {"outcome": False}}
    )
)
for extra in (["--check-config"], []):
    result = subprocess.run(
        command + ["--config", str(config), *extra], capture_output=True, text=True, timeout=20
    )
    assert result.returncode == 0, result.stderr
summary = json.loads((out / "summary.json").read_text())
assert summary["success"] and summary["memory"]["4096"] == 0, summary
assert summary["backend_options"] == {"outcome": False}, summary
observations = []
for limit in (1, 3, 1024):
    audit = (out / f"batches-{limit}.json").resolve()
    config.write_text(
        json.dumps(
            {
                "schema": 1,
                "backend": "custom_backend:Configured",
                "backend_options": {"outcome": False, "audit_path": str(audit)},
                "backend_execution": {"max_batch_operations": limit},
            }
        )
    )
    result = subprocess.run(
        command + ["--config", str(config)], capture_output=True, text=True, timeout=20
    )
    assert result.returncode == 0, result.stderr
    summary = json.loads((out / "summary.json").read_text())
    batches = json.loads(audit.read_text())
    assert all(0 < len(batch["operations"]) <= limit for batch in batches)
    operations = [operation for batch in batches for operation in batch["operations"]]
    for batch in batches:
        assert all(op["kind"] != "measure" for op in batch["operations"][:-1])
    assert operations[-1]["tick"] == summary["stop_tick"]
    events = [json.loads(line) for line in (out / "trace.jsonl").read_text().splitlines()]
    timing = [
        (event["tick"], event["kind"], event["id"], event["value"])
        for event in events
        if event["kind"]
        in {
            "InstructionRetired",
            "OperationStart",
            "OperationEnd",
            "MeasurementSampled",
            "ResultReady",
            "MeasurementRegisterUpdated",
            "ExecutionFlagsUpdated",
        }
    ]
    observations.append((summary["stop_tick"], summary["memory"], operations, timing))
assert observations[0] == observations[1] == observations[2]
for execution in (
    {"max_batch_operations": 0},
    {"max_batch_operations": True},
    {"max_batch_operations": 4294967296},
    {"unknown": 1},
    [],
):
    config.write_text(json.dumps({"backend_execution": execution}))
    result = subprocess.run(
        command + ["--config", str(config), "--check-config"],
        capture_output=True,
        text=True,
        timeout=20,
    )
    assert result.returncode == 2, result.stderr
config.write_text(
    json.dumps(
        {
            "schema": 1,
            "backend": "custom_backend:Configured",
            "backend_options": {"outcome": "false"},
        }
    )
)
for extra in (["--check-config"], []):
    result = subprocess.run(
        command + ["--config", str(config), *extra], capture_output=True, text=True, timeout=20
    )
    assert result.returncode == 2 and "backend_options.outcome" in result.stderr, result.stderr
for backend, message in [
    ("custom_backend:Incomplete", "requires callable validate"),
    ("nonexistent_qsbit_test_module:Backend", "No module named"),
    ("bad:format:extra", "MODULE:CLASS"),
]:
    result = subprocess.run(
        command + ["--backend", backend], capture_output=True, text=True, timeout=20
    )
    assert result.returncode == 2 and message in result.stderr, result.stderr
print("PASS external backend, missing dependency, and invalid contract")
