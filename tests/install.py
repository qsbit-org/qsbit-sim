"""Exercise a fixed-prefix installation with isolated, offline Python fixtures."""

import argparse
import importlib.metadata
import json
import os
import shutil
import signal
import subprocess
import tempfile
import time
import venv
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--program", type=Path, required=True)
parser.add_argument("--bindir", required=True)
parser.add_argument("--libdir", required=True)
parser.add_argument("--python", action="store_true")
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="qsbit-install-") as temporary:
    directory = Path(temporary)
    prefix = directory / "prefix with spaces"
    root = prefix / args.libdir / "qsbit-sim"
    if args.python:
        # Install dependency files already provisioned for the build, without network access.
        # The full integration workflow installs the locked wheel normally with uv/pip.
        venv.EnvBuilder(with_pip=False).create(root / "venv")
        interpreter = root / "venv/bin/python"
        site = Path(
            subprocess.check_output(
                [interpreter, "-I", "-c", "import sysconfig; print(sysconfig.get_path('purelib'))"],
                text=True,
            ).strip()
        )
        for name in (
            "jsonschema",
            "attrs",
            "jsonschema-specifications",
            "referencing",
            "rpds-py",
            "typing-extensions",
        ):
            try:
                distribution = importlib.metadata.distribution(name)
            except importlib.metadata.PackageNotFoundError:
                if name == "typing-extensions":
                    continue  # Conditional dependency on older Python versions.
                raise
            for item in distribution.files or ():
                if ".." in item.parts or item.suffix == ".pyc":
                    continue
                source = Path(distribution.locate_file(item))
                destination = site / item
                if source.is_file():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, destination)
        backend = site / "qsbit_backend"
        shutil.copytree(
            args.source / "python/qsbit_backend",
            backend,
            ignore=shutil.ignore_patterns("__pycache__"),
        )
        # Installation must reject the exact stale-adapter failure seen in Studio.
        stim = backend / "stim.py"
        original = stim.read_bytes()
        stim.write_text("# stale adapter\n")
        failed = subprocess.run(
            ["cmake", "--install", args.build, "--prefix", prefix], capture_output=True, text=True
        )
        assert failed.returncode != 0 and "Stale backend" in failed.stderr, failed.stderr
        stim.write_bytes(original)
    subprocess.run(["cmake", "--install", args.build, "--prefix", prefix], check=True)
    simulator = prefix / args.bindir / "qsbit-sim"
    # A symlink and unrelated cwd must not change installation discovery.
    alias = directory / "sim-alias"
    alias.symlink_to(simulator)
    environment = dict(os.environ)
    for key in ("QSBIT_PYTHON", "VIRTUAL_ENV", "PYTHONPATH", "PYTHONHOME", "PYTHONUSERBASE"):
        environment.pop(key, None)

    def command(*arguments, env=environment, success=True):
        result = subprocess.run(
            [alias, *arguments], cwd=directory, env=env, capture_output=True, text=True
        )
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result

    info = json.loads(command("--installation-info").stdout)
    assert info["schema"] == info["player_api"] == 1 and info["installed"]
    assert info["python_enabled"] == args.python
    assert Path(info["web_root"]) == (root / "web").resolve()
    assert (Path(info["web_root"]) / "trace-player.js").read_bytes() == (
        args.source / "web/trace-player.js"
    ).read_bytes()
    command("--program", str(args.program), "--backend", "mock", "--summary", "summary.json")
    assert json.loads((directory / "summary.json").read_text())["success"]
    # A controlled native entry point verifies the launcher's process contract.
    native = root / "qsbit-sim-native"
    saved_native = root / "native-saved"
    native.rename(saved_native)
    try:
        native.write_text('#!/bin/sh\n[ "$1" = "argument with spaces" ] || exit 91\nexit 17\n')
        native.chmod(0o755)
        assert command("argument with spaces", success=False).returncode == 17
        native.write_text('#!/bin/sh\necho $$ > "$1"\nexec sleep 60\n')
        pid_file = directory / "native.pid"
        process = subprocess.Popen([alias, pid_file], cwd=directory, env=environment)
        try:
            deadline = time.monotonic() + 5
            while not pid_file.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            assert int(pid_file.read_text()) == process.pid
            process.terminate()
            assert process.wait(timeout=5) == -signal.SIGTERM
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
    finally:
        native.unlink()
        saved_native.rename(native)
    if args.python:
        assert Path(info["backend"]) == backend
        assert Path(info["python"]) == interpreter
        poison = directory / "qsbit_backend"
        poison.mkdir()
        (poison / "__init__.py").write_text("raise RuntimeError('wrong backend loaded')\n")
        polluted = dict(
            environment,
            QSBIT_PYTHON="/missing/python",
            VIRTUAL_ENV="/missing/venv",
            PYTHONHOME="/missing/home",
            PYTHONPATH=str(directory),
            PYTHONUSERBASE=str(directory),
        )
        command("--list-backends", env=polluted)
        # Also exercise the native binary: isolation cannot depend only on a shell wrapper.
        result = subprocess.run(
            [root / "qsbit-sim-native", "--list-backends"],
            cwd=directory,
            env=polluted,
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, result.stderr
        saved = backend.with_name("backend-hidden")
        backend.rename(saved)
        command("--installation-info", success=False)
        saved.rename(backend)
        interpreter.unlink()
        command("--list-backends", success=False)
    print("Installed runtime discovery, resource pairing and Python isolation passed.")
