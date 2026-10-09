"""Run a relocated CMake installation outside the source and build directories."""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--program", type=Path, required=True)
parser.add_argument("--bindir", required=True)
parser.add_argument("--python", action="store_true")
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="qsbit-install-") as temporary:
    directory = Path(temporary)
    prefix = directory / "prefix"
    subprocess.run(["cmake", "--install", str(args.build), "--prefix", str(prefix)], check=True)
    relocated = directory / "relocated"
    prefix.rename(relocated)
    simulator = relocated / args.bindir / "qsbit-sim"
    environment = dict(os.environ)
    environment.pop("QSBIT_PYTHON", None)
    environment.pop("PYTHONPATH", None)
    environment.pop("VIRTUAL_ENV", None)
    if sys.prefix != sys.base_prefix:
        environment["VIRTUAL_ENV"] = sys.prefix
    else:
        environment["QSBIT_PYTHON"] = sys.executable
    summary = directory / "summary.json"
    subprocess.run(
        [
            str(simulator),
            "--program",
            str(args.program),
            "--backend",
            "mock",
            "--summary",
            str(summary),
        ],
        cwd=directory,
        env=environment,
        check=True,
    )
    assert json.loads(summary.read_text())["success"]
    if args.python:
        subprocess.run(
            [str(simulator), "--list-backends"],
            cwd=directory,
            env=environment,
            check=True,
        )
        invalid = dict(environment, QSBIT_PYTHON=str(directory / "missing-python"))
        result = subprocess.run(
            [str(simulator), "--list-backends"],
            cwd=directory,
            env=invalid,
            capture_output=True,
            text=True,
        )
        assert result.returncode == 2 and "Python interpreter does not exist" in result.stderr
