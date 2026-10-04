"""Backend discovery, option validation and CLI configuration."""

import argparse
import json
import subprocess
import sys
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path
from unittest.mock import patch

from backend_test_utils import measure

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from jsonschema import Draft202012Validator
from qsbit_backend import registry

parser = argparse.ArgumentParser()
parser.add_argument("--simulator", type=Path, required=True)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--aer", action="store_true")
parser.add_argument("--stim", action="store_true")
args = parser.parse_args()

THERMAL = {
    "method": "density_matrix",
    "noise": {
        "model": "thermal_relaxation",
        "qubits": [{"qubit": 0, "t1_ns": 100, "t2_ns": 200, "excited_state_population": 0}],
    },
}


class Configuration(unittest.TestCase):
    def test_discovery_without_numerical_imports(self):
        for command in ("list", "help", "schema", "generate"):
            json.loads(registry.inspect_backend("stim", command))
        self.assertFalse(any(name in sys.modules for name in ("stim", "qiskit", "qiskit_aer")))

    def test_unknown_fields_models_and_values(self):
        for name, value in [
            ("aer", {"typo": 1}),
            ("stim", THERMAL),
            ("pulse", {"noise": {"model": "none"}}),
            ("stim", {"noise": {"model": "depolarizing", "after_gate_probability": 2}}),
            ("aer", {"noise": {"model": "thermal_relaxation", "qubits": []}}),
            ("aer", {"max_parallel_threads": True}),
            ("aer", []),
            ("aer", {"max_batch_operations": 1024}),
            ("aer", {"max_parallel_threads": float("nan")}),
        ]:
            with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                registry.options(name, value)
        incomplete = deepcopy(THERMAL)
        del incomplete["noise"]["qubits"][0]["t1_ns"]
        with self.assertRaisesRegex(ValueError, "backend_options.noise.qubits"):
            registry.options("aer", incomplete)

    def test_defaults_and_editor_schema(self):
        schema = json.loads(registry.inspect_backend("aer", "schema"))
        validator = Draft202012Validator(schema)
        for name in ("aer", "pulse", "stim", "mock"):
            config = json.loads(registry.inspect_backend(name, "generate"))
            validator.validate(config)
        validator.validate({"backend": "aer", "backend_options": THERMAL})
        self.assertFalse(validator.is_valid({"backend": "stim", "backend_options": THERMAL}))
        original = {}
        self.assertEqual(registry.options("aer", original)["noise"], {"model": "none"})
        self.assertEqual(original, {})

    def test_entry_point_discovery_and_collision(self):
        sys.path.insert(0, str(Path(__file__).resolve().parent / "fixtures"))
        from custom_backend import Configured

        class Entry:
            name = "external"

            def load(self):
                return lambda: dict(Configured.describe(), factory="custom_backend:Configured")

        with patch.object(registry.metadata, "entry_points", return_value=[Entry()]):
            backend, resolved = registry.create("external", {"outcome": True})
            backend.reset(1, 4)
            self.assertEqual(measure(backend, [{"target": 0}]), [True])
            self.assertEqual(resolved, {"outcome": True})
            config = json.loads(registry.inspect_backend("external", "generate"))
            self.assertIsNone(config["backend_options"]["outcome"])
            schema = json.loads(registry.inspect_backend("external", "schema"))
            validator = Draft202012Validator(schema)
            config["backend_options"] = {"outcome": False}
            validator.validate(config)
            config["backend_options"] = {"outcome": "false"}
            self.assertFalse(validator.is_valid(config))
            Entry.name = "aer"
            with self.assertRaisesRegex(ValueError, "duplicate"):
                registry.descriptions()

    def test_missing_dependency_install_hint(self):
        with patch.object(registry, "missing", return_value=["stim"]):
            with self.assertRaisesRegex(ValueError, "pip install"):
                registry.create("stim", {})

    def test_cli_json_and_precheck(self):
        for command in (
            "--list-backends",
            "--help-backend",
            "--backend-schema",
            "--generate-config",
        ):
            result = subprocess.run(
                [str(args.simulator), "--backend", "stim", command],
                capture_output=True,
                text=True,
                timeout=20,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            json.loads(result.stdout)
        result = subprocess.run(
            [
                str(args.simulator),
                "--backend",
                "mock",
                "--program",
                str(args.build / "examples/bell-state/program.elf"),
                "--check-config",
            ],
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


class Aer(unittest.TestCase):
    def test_invalid_physical_parameters(self):
        for modify in (
            lambda c: c.update(method="statevector"),
            lambda c: c["noise"]["qubits"][0].update(t2_ns=201),
            lambda c: c["noise"]["qubits"].append(dict(c["noise"]["qubits"][0])),
        ):
            config = deepcopy(THERMAL)
            modify(config)
            with self.assertRaises(ValueError):
                registry.create("aer", config)
        config = deepcopy(THERMAL)
        config["noise"]["qubits"][0]["qubit"] = 2
        backend, _ = registry.create("aer", config)
        with self.assertRaisesRegex(ValueError, "outside"):
            backend.reset(2, 1)


class Integration(unittest.TestCase):
    def test_precheck_rejects_invalid_config_without_outputs(self):
        with tempfile.TemporaryDirectory(dir=args.build) as directory:
            root = Path(directory)
            cases = [("mock", {}, [10, 10])]
            if args.aer:
                bad = deepcopy(THERMAL)
                bad["noise"]["qubits"][0]["t2_ns"] = 201
                cases.append(("aer", bad, []))
            if args.stim:
                cases.append(("stim", THERMAL, []))
            for name, options, resets in cases:
                config = {
                    "schema": 1,
                    "backend": name,
                    "backend_options": options,
                    "program": str((args.build / "examples/bell-state/program.elf").resolve()),
                    "resets": resets,
                    "summary": "summary.json",
                    "trace": "trace.jsonl",
                }
                path = root / "run.json"
                path.write_text(json.dumps(config))
                for extra in (["--check-config"], []):
                    result = subprocess.run(
                        [str(args.simulator), "--config", str(path), *extra],
                        capture_output=True,
                        text=True,
                        timeout=20,
                    )
                    self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse((root / "summary.json").exists())
                self.assertFalse((root / "trace.jsonl").exists())

    def test_numerical_cli(self):
        out = args.build / "backend-tests"
        out.mkdir(exist_ok=True)
        cases = []
        if args.stim:
            cases += [("stim", {}, "bell"), ("stim", {}, "feedback")]
        if args.aer:
            cases += [("aer", {"method": "density_matrix"}, "bell"), ("aer", THERMAL, "bell")]
        for index, (name, options, example) in enumerate(cases):
            directory = "bell-state" if example == "bell" else "measurement-feedback"
            config = {
                "schema": 1,
                "program": str((args.build / "examples" / directory / "program.elf").resolve()),
                "backend": name,
                "backend_options": options,
                "profile": {"start": 200},
                "trace": f"{index}.jsonl",
                "summary": f"{index}.json",
                "inspect": [4096, 4100],
            }
            path = out / f"run-{index}.json"
            path.write_text(json.dumps(config))
            for extra in (["--check-config"], []):
                result = subprocess.run(
                    [str(args.simulator), "--config", str(path), *extra],
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
            summary = json.loads((out / f"{index}.json").read_text())
            self.assertTrue(summary["success"])
            self.assertEqual(summary["backend_options"], registry.options(name, options))
            self.assertEqual(summary["statevector"], [])
            if name == "aer":
                self.assertEqual(len(summary["density_matrix"]), 4)
            if options.get("noise", {}).get("model", "none") == "none" and example == "bell":
                self.assertEqual(summary["memory"]["4096"], summary["memory"]["4100"])
            if example == "feedback":
                self.assertEqual(summary["memory"]["4096"], 1)


suite = unittest.TestSuite()
for cls in [Configuration, *([Aer] if args.aer else []), Integration]:
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(cls))
sys.exit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
