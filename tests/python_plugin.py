"""Exercise an external backend without importing installed numerical packages."""
import argparse
import importlib
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
for name in ("simulator", "source", "build"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
sys.path.insert(0, str(a.source / "python"))
importlib.import_module("qsbit_backend")
assert all(name not in sys.modules for name in
           ("qsbit_backend.aer", "qsbit_backend.pulse", "qiskit", "qiskit_aer", "scipy", "numpy"))
out = a.build / "plugin-test"
out.mkdir(exist_ok=True)
command = [str(a.simulator), "--program", str(a.build / "examples/feedback.elf"),
           "--python-path", str(a.source / "tests/fixtures"),
           "--trace", str(out / "trace.jsonl"), "--summary", str(out / "summary.json"),
           "--inspect", "4096"]
result = subprocess.run(command + ["--backend", "custom_backend:Backend"],
                        capture_output=True, text=True, timeout=20)
assert result.returncode == 0, result.stderr
summary = json.loads((out / "summary.json").read_text())
assert summary["success"] and summary["memory"]["4096"] == 1, summary
assert summary["statevector"] == [], summary
for backend, message in [("custom_backend:Incomplete", "requires callable validate"),
                         ("nonexistent_qsbit_test_module:Backend", "No module named"),
                         ("bad:format:extra", "MODULE:CLASS")]:
    result = subprocess.run(command + ["--backend", backend], capture_output=True,
                            text=True, timeout=20)
    assert result.returncode == 2 and message in result.stderr, result.stderr
print("PASS external backend, missing dependency, and invalid contract")
