"""Run a short AllXY experiment through the CLI and plotting command."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
for option in ("simulator", "source", "build"):
    parser.add_argument("--" + option, type=Path, required=True)
args = parser.parse_args()
output = (args.build / "allxy-test").resolve()
output.mkdir(exist_ok=True)
config = json.loads((args.source / "examples/quma/run.json").read_text())
config["program"] = str((args.build / "examples/quma/allxy.elf").resolve())
config["summary"] = str(output / "result.json")
config["simulation"]["repetitions"] = 4
path = output / "run.json"
path.write_text(json.dumps(config))
subprocess.run([str(args.simulator), "--config", str(path)], check=True, timeout=120)
subprocess.run(
    [
        sys.executable,
        str(args.source / "examples/quma/plot.py"),
        "--results",
        config["summary"],
        "--output",
        str(output / "figs"),
    ],
    check=True,
    timeout=30,
)
