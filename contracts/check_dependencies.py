"""Check declared C++ module dependencies and standalone internal headers."""

import argparse
import fnmatch
import json
import re
import shlex
import subprocess
import tempfile
from pathlib import Path


def violations(root, rules):
    root = root.resolve()
    files = {}
    for module, spec in rules["modules"].items():
        for pattern in spec["files"]:
            for path in root.glob(pattern):
                if path.is_file():
                    if path in files:
                        raise ValueError(f"duplicate module assignment: {path}")
                    files[path] = module
    errors = []
    for base in rules["roots"]:
        for path in (root / base).rglob("*"):
            if path.suffix in (".hpp", ".cpp") and path.is_file() and path not in files:
                errors.append(f"unclassified source: {path.relative_to(root)}")
    for path, module in files.items():
        spec = rules["modules"][module]
        for line, text in enumerate(path.read_text().splitlines(), 1):
            match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', text)
            if not match:
                continue
            name = match[1]
            if Path(name).is_absolute():
                errors.append(f"{path.relative_to(root)}:{line}: absolute include path {name}")
                continue
            candidates = [path.parent / name] + [root / base / name for base in rules["roots"]]
            target = next((p.resolve() for p in candidates if p.is_file()), None)
            dependency = files.get(target)
            if target and dependency is None:
                errors.append(f"{path.relative_to(root)}:{line}: unclassified header {name}")
            elif dependency and dependency != module and dependency not in spec.get("allows", []):
                errors.append(
                    f"{path.relative_to(root)}:{line}: {module} must not include {dependency}: {name}"
                )
            elif (
                not target
                and ("/" in name or name in ("systemc", "systemc.h", "Python.h"))
                and not any(fnmatch.fnmatch(name, pattern) for pattern in spec.get("external", []))
            ):
                errors.append(
                    f"{path.relative_to(root)}:{line}: undeclared external dependency {name}"
                )
    return files, errors


def headers(root, files, database, rules):
    entries = json.loads(database.read_text())
    commands = {}
    for entry in entries:
        module = files.get(Path(entry["file"]).resolve())
        if module is not None:
            commands.setdefault(module, entry)
    for path, module in files.items():
        if path.suffix != ".hpp":
            continue
        entry = commands.get(rules["modules"][module].get("header_command", module))
        if entry is None:
            entry = next(iter(commands.values()))
        args = entry.get("arguments") or shlex.split(entry["command"])
        flags = []
        skip = False
        for arg in args[1:]:
            if skip:
                skip = False
            elif arg in ("-o", "-MF", "-MT", "-MQ"):
                skip = True
            elif arg not in ("-c", "-MD", "-MMD", entry["file"]):
                flags.append(arg)
        subprocess.run(
            [args[0], *flags, "-Wno-pragma-once-outside-header", "-fsyntax-only", "-x", "c++", "-"],
            input=f'#include "{path}"\n',
            text=True,
            cwd=entry["directory"],
            check=True,
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--headers", type=Path, help="compile_commands.json")
    args = parser.parse_args()
    if args.self_test:
        with tempfile.TemporaryDirectory() as directory:
            probe = Path(directory) / "source"
            probe.mkdir()
            (probe / "high.hpp").write_text("#pragma once\n")
            (probe / "low.cpp").write_text('#include "high.hpp"\n')
            rules = {
                "roots": ["."],
                "modules": {"low": {"files": ["low.cpp"]}, "high": {"files": ["high.hpp"]}},
            }
            assert violations(probe, rules)[1], "accepted a forbidden include"
            rules["modules"]["low"]["allows"] = ["high"]
            assert not violations(probe, rules)[1], "rejected a declared dependency"
            alias = Path(directory) / "alias"
            alias.symlink_to(probe, target_is_directory=True)
            assert not violations(alias, rules)[1], "rejected a symlinked source directory"
        return
    root = args.root.resolve()
    rules = json.loads((root / "architecture.json").read_text())
    files, errors = violations(root, rules)
    if errors:
        parser.exit(1, "\n".join(errors) + "\n")
    if args.headers:
        headers(root, files, args.headers, rules)


if __name__ == "__main__":
    main()
