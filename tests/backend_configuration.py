"""Configuration discovery, adapter validation and numerical backend checks."""

import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

parser = argparse.ArgumentParser()
parser.add_argument("--simulator", type=Path, required=True)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--aer", action="store_true")
parser.add_argument("--stim", action="store_true")
args = parser.parse_args()
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from qsbit_backend import registry
from jsonschema import Draft202012Validator


def gate(operation, targets=(0,), amplitude=0):
    return {"kind": "gate", "operation": operation, "targets": list(targets), "amplitude": amplitude}


THERMAL = {"method": "density_matrix", "noise": {"model": "thermal_relaxation", "qubits": [
    {"qubit": 0, "t1_ns": 100, "t2_ns": 200, "excited_state_population": 0}]}}


class Configuration(unittest.TestCase):
    def test_discovery_without_numerical_imports(self):
        for command in ("list", "help", "schema", "generate"):
            json.loads(registry.inspect_backend("stim", command))
        self.assertFalse(any(name in sys.modules for name in ("stim", "qiskit", "qiskit_aer")))

    def test_unknown_fields_models_and_values(self):
        for name, value in [("aer", {"typo": 1}), ("stim", THERMAL),
                            ("pulse", {"noise": {"model": "none"}}),
                            ("stim", {"noise": {"model": "depolarizing", "after_gate_probability": 2}}),
                            ("aer", {"noise": {"model": "thermal_relaxation", "qubits": []}}),
                            ("aer", {"max_parallel_threads": True}), ("aer", []),
                            ("aer", {"max_parallel_threads": float("nan")})]:
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
            self.assertEqual(backend.measure([{"target": 0}]), [True])
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
        for command in ("--list-backends", "--help-backend", "--backend-schema", "--generate-config"):
            result = subprocess.run([str(args.simulator), "--backend", "stim", command],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            json.loads(result.stdout)
        result = subprocess.run([str(args.simulator), "--backend", "mock", "--program",
            str(args.build / "examples/bell-state/program.elf"), "--check-config"],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)


class Aer(unittest.TestCase):
    def test_relaxation_and_interval_composition(self):
        backend, _ = registry.create("aer", THERMAL)
        backend.reset(1, 123)
        backend.apply([gate("x")])
        backend.evolve(0, 100, [])
        self.assertAlmostEqual(backend.density_matrix()[1][1].real, math.exp(-1), places=12)
        self.assertEqual(backend.state(), [])
        backend.reset(1, 123)
        backend.apply([gate("x")])
        backend.evolve(0, 30, [])
        backend.evolve(30, 100, [])
        self.assertAlmostEqual(backend.density_matrix()[1][1].real, math.exp(-1), places=12)
        backend.reset(1, 123)
        backend.evolve(0, 100, [])
        self.assertAlmostEqual(backend.density_matrix()[0][0].real, 1, places=12)

    def test_coherence_equilibrium_and_unlisted_qubit(self):
        config = deepcopy(THERMAL)
        config["noise"]["qubits"][0]["excited_state_population"] = 0.2
        backend, _ = registry.create("aer", config)
        backend.reset(2, 10)
        backend.apply([gate("h"), gate("x", (1,))])
        backend.evolve(0, 100, [])
        rho = backend.density_matrix()
        self.assertAlmostEqual(rho[3][3].real, 0.2 + 0.3 * math.exp(-1), places=12)
        self.assertAlmostEqual(rho[2][3].real, 0.5 * math.exp(-0.5), places=12)
        self.assertAlmostEqual(rho[0][0].real + rho[1][1].real, 0, places=12)

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

    def test_measurement_collapse_and_ideal_mode(self):
        for config in ({}, {"method": "density_matrix"}):
            backend, _ = registry.create("aer", config)
            backend.reset(2, 3)
            backend.apply([gate("h"), gate("cx", (0, 1))])
            refs = [{"target": 0}, {"target": 1}]
            bits = backend.measure(refs)
            self.assertEqual(bits[0], bits[1])
            self.assertEqual(bits, backend.measure(refs))


class Stim(unittest.TestCase):
    def test_measurements_release_history_without_changing_sampling(self):
        import stim
        for probability in (0, 0.125):
            backend, _ = registry.create("stim", {"noise": {
                "model": "depolarizing", "after_gate_probability": probability}})
            backend.reset(3, 4321)
            reference = stim.TableauSimulator(seed=4321)
            reference.set_num_qubits(3)
            gates = [gate("h"), gate("cx", (0, 1)), gate("h", (2,)), gate("cx", (1, 2))]
            circuit = stim.Circuit()
            for event in gates:
                circuit.append(event["operation"], event["targets"])
                circuit.append("DEPOLARIZE1", event["targets"], probability)
            for iteration in range(512):
                backend.apply(gates)
                reference.do(circuit)
                targets = ([2, 0], [1], [1, 0, 2])[iteration % 3]
                self.assertEqual(backend.measure([{"target": q} for q in targets]),
                                 reference.measure_many(*targets))
                self.assertEqual(backend._simulator.current_measurement_record(), [])
            self.assertEqual(backend._simulator.current_inverse_tableau(),
                             reference.current_inverse_tableau())

    def test_bell_collapse_seed_and_reset(self):
        backend, _ = registry.create("stim", {})
        outcomes = []
        for _ in range(2):
            backend.reset(2, 4321)
            backend.apply([gate("h"), gate("cx", (0, 1))])
            refs = [{"target": 0}, {"target": 1}]
            bits = backend.measure(refs)
            self.assertEqual(bits[0], bits[1])
            self.assertEqual(bits, backend.measure(refs))
            outcomes.append(bits)
        self.assertEqual(*outcomes)

    def test_clifford_rotations_and_rejections(self):
        backend, _ = registry.create("stim", {})
        for operation in ("rx", "ry"):
            backend.reset(1, 5)
            backend.apply([gate(operation, amplitude=math.pi / 2)] * 2)
            self.assertEqual(backend.measure([{"target": 0}]), [True])
            backend.apply([gate(operation, amplitude=-math.pi)])
            self.assertEqual(backend.measure([{"target": 0}]), [False])
        for event in (gate("t"), gate("rx", amplitude=math.pi / 4), {"kind": "pulse"}):
            with self.assertRaises(ValueError):
                backend.validate(event)
        with self.assertRaises(ValueError):
            backend.evolve(5, 4, [])

    def test_rotation_signs(self):
        backend, _ = registry.create("stim", {})
        for sign in (-1, 1):
            for operation in ("rx", "ry", "rz"):
                backend.reset(1, 2)
                before = [gate("h")] if operation == "rz" else []
                after = {"rx": [gate("s"), gate("h")], "ry": [gate("h")],
                         "rz": [gate("sdg"), gate("h")]}[operation]
                backend.apply(before + [gate(operation, amplitude=sign * math.pi / 2)] + after)
                self.assertEqual(backend.measure([{"target": 0}]), [sign < 0])

    def test_depolarization_distribution(self):
        backend, _ = registry.create("stim", {"noise": {
            "model": "depolarizing", "after_gate_probability": 1}})
        backend.reset(4096, 9001)
        backend.apply([gate("id", (q,)) for q in range(4096)])
        bits = backend.measure([{"target": q} for q in range(4096)])
        self.assertLess(abs(sum(bits) / len(bits) - 2 / 3), 0.04)


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
                config = {"schema": 1, "backend": name, "backend_options": options,
                    "program": str((args.build / "examples/bell-state/program.elf").resolve()),
                    "resets": resets, "summary": "summary.json", "trace": "trace.jsonl"}
                path = root / "run.json"
                path.write_text(json.dumps(config))
                for extra in (["--check-config"], []):
                    result = subprocess.run([str(args.simulator), "--config", str(path), *extra],
                                            capture_output=True, text=True, timeout=20)
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
            cases += [("aer", {"method": "density_matrix"}, "bell"),
                      ("aer", THERMAL, "bell")]
        for index, (name, options, example) in enumerate(cases):
            directory = "bell-state" if example == "bell" else "measurement-feedback"
            config = {"schema": 1, "program": str((args.build / "examples" / directory / "program.elf").resolve()),
                "backend": name, "backend_options": options, "profile": {"start": 200},
                "trace": f"{index}.jsonl", "summary": f"{index}.json", "inspect": [4096, 4100]}
            path = out / f"run-{index}.json"
            path.write_text(json.dumps(config))
            for extra in (["--check-config"], []):
                result = subprocess.run([str(args.simulator), "--config", str(path), *extra],
                                        capture_output=True, text=True, timeout=30)
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
            if example == "bell":
                trace = [json.loads(line) for line in (out / f"{index}.jsonl").read_text().splitlines()]
                starts = [event["tick"] for event in trace if event["kind"] == "OperationStart"]
                self.assertEqual(starts, [360, 400, 440, 440])


suite = unittest.TestSuite()
for cls in [Configuration, *([Aer] if args.aer else []), *([Stim] if args.stim else []), Integration]:
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(cls))
sys.exit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
