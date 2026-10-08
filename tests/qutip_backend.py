"""QuTiP dynamics, acquisition records and the backend bridge between C++ and Python."""

import argparse
import json
import math
import subprocess
import sys
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path

import numpy as np
from scipy.linalg import expm

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from qsbit_backend.qutip import QutipBackend

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--simulator", type=Path, required=True)
parser.add_argument("--assembler", required=True)
parser.add_argument("--linker", required=True)
args = parser.parse_args()


def model(count=1):
    return {
        "subsystems": [
            dict(
                levels=2,
                detuning_rad_ns=0,
                anharmonicity_rad_ns=0,
                t1_ns=None,
                tphi_ns=None,
                thermal_occupation=0,
                initial_populations=[1, 0],
            )
            for _ in range(count)
        ],
        "waveforms": [dict(operation="drive", shape="square", phase_rad=0, frequency_rad_ns=0)],
        "solver": dict(rtol=1e-10, atol=1e-12, max_step_ns=1, max_steps=100000),
        "max_dimension": 32,
    }


def evolve(start, end, drives=(), acquisitions=()):
    return dict(
        kind="evolve", start=start, tick=end, drives=list(drives), acquisitions=list(acquisitions)
    )


def pulse(start=0, end=20, **kwargs):
    return dict(
        kind="pulse",
        operation="drive",
        targets=[0],
        axis="x",
        amplitude=math.pi / 20,
        port=0,
        id=1,
        start=start,
        end=end,
        **kwargs,
    )


class Dynamics(unittest.TestCase):
    def test_measurement_records_restart_after_reset(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "measurements.jsonl"
            backend = QutipBackend(dict(model(), measurements=str(path)))
            for epoch in (1, 2):
                previous = path.read_text() if path.exists() else None
                backend.reset(1, 1)
                self.assertEqual(path.read_text() if path.exists() else None, previous)
                for measurement in (1, 2):
                    backend.execute(
                        epoch,
                        [
                            dict(
                                kind="measure",
                                tick=measurement,
                                references=[dict(epoch=epoch, measurement=measurement, target=0)],
                            )
                        ],
                    )
                    records = [json.loads(line) for line in path.read_text().splitlines()]
                    self.assertEqual(
                        [r["measurement"] for r in records], list(range(1, measurement + 1))
                    )
                    self.assertTrue(all(r["epoch"] == epoch for r in records))

    def test_gaussian_and_drag_partition_preserves_envelope_origin(self):
        for shape in ("gaussian", "drag"):
            config = model()
            config["waveforms"][0].update(shape=shape, sigma_ns=5)
            if shape == "drag":
                config["waveforms"][0]["beta_ns"] = 2
            results = []
            for boundaries in ((10, 30), (10, 13, 21, 30)):
                backend = QutipBackend(config)
                backend.reset(1, 1)
                for start, end in zip(boundaries, boundaries[1:]):
                    backend.execute(1, [evolve(start, end, [pulse(10, 30)])])
                results.append(np.array(backend.density_matrix()))
            np.testing.assert_allclose(*results, atol=2e-8)
            if shape == "gaussian":
                theta = math.pi / 20 * 5 * math.sqrt(2 * math.pi) * math.erf(math.sqrt(2))
                self.assertAlmostEqual(results[0][1, 1].real, math.sin(theta / 2) ** 2, places=8)

    def test_relaxation_dephasing_and_reset(self):
        config = model()
        config["subsystems"][0].update(t1_ns=100, tphi_ns=80)
        backend = QutipBackend(config)
        backend.reset(1, 7)
        backend.execute(1, [evolve(0, 10, [pulse(0, 10)])])
        before = np.array(backend.density_matrix())
        backend.execute(1, [evolve(10, 110)])
        after = np.array(backend.density_matrix())
        self.assertAlmostEqual(after[1, 1].real, before[1, 1].real * math.exp(-1), places=8)
        self.assertAlmostEqual(
            abs(after[0, 1]), abs(before[0, 1]) * math.exp(-0.5 - 1.25), places=8
        )
        backend.reset(1, 7)
        np.testing.assert_allclose(backend.density_matrix(), [[1, 0], [0, 0]], atol=1e-12)

    def test_exchange_and_simultaneous_noncommuting_drives(self):
        config = model(2)
        config["subsystems"][0].update(
            levels=3, initial_populations=[1, 0, 0], anharmonicity_rad_ns=-0.3
        )
        config["couplings"] = [dict(targets=[0, 1], strength_rad_ns=0.05)]
        backend = QutipBackend(config)
        backend.reset(2, 7)
        x = pulse()
        z = dict(pulse(), axis="z", port=1, id=2, amplitude=0.1)
        backend.execute(1, [evolve(0, 20, [x, z])])
        a = np.kron(np.eye(2), np.diag([1, math.sqrt(2)], 1))
        b = np.kron([[0, 1], [0, 0]], np.eye(3))
        h = math.pi / 40 * (a + a.T) + 0.1 * a.T @ a + 0.05 * (a.T @ b + b.T @ a)
        n = a.T @ a
        h -= 0.15 * n @ (n - np.eye(6))
        state = expm(-20j * h)[:, 0]
        np.testing.assert_allclose(
            backend.density_matrix(), np.outer(state, state.conj()), atol=2e-8
        )

    def test_carrier_resonance_and_thermal_equilibrium(self):
        config = model()
        config["subsystems"][0]["detuning_rad_ns"] = 0.3
        config["waveforms"][0]["frequency_rad_ns"] = 0.3
        backend = QutipBackend(config)
        backend.reset(1, 7)
        backend.execute(1, [evolve(40, 60, [pulse(40, 60)])])
        self.assertAlmostEqual(backend.density_matrix()[1][1].real, 1, places=8)
        config["subsystems"][0].update(t1_ns=10, thermal_occupation=0.2)
        backend = QutipBackend(config)
        backend.reset(1, 7)
        backend.execute(1, [evolve(0, 200)])
        self.assertAlmostEqual(backend.density_matrix()[1][1].real, 0.2 / 1.4, places=8)

    def test_samples_measurement_collapse_and_readout(self):
        config = model()
        config["waveforms"][0].update(
            shape="samples", times_ns=[0, 10, 20], iq=[[0, 0], [2, 0], [0, 0]]
        )
        config["readout"] = [
            dict(
                target=0,
                means=[[-1, 0], [1, 0]],
                ringup_ns=2,
                noise_std_sqrt_ns=0,
                sample_interval_ns=2,
                rotation_rad=0,
                threshold=0,
            )
        ]
        acquire = dict(
            kind="acquire",
            operation="measure",
            targets=[0],
            port=0,
            id=2,
            start=20,
            end=40,
            measurement=1,
        )
        backend = QutipBackend(config)
        for _ in range(2):
            backend.reset(1, 3)
            bits = backend.execute(
                1,
                [
                    evolve(0, 20, [pulse()]),
                    evolve(20, 40, acquisitions=[acquire]),
                    dict(
                        kind="measure", tick=40, references=[dict(epoch=1, measurement=1, target=0)]
                    ),
                ],
            )
            self.assertEqual(bits, [True])
            np.testing.assert_allclose(backend.density_matrix(), [[0, 0], [0, 1]], atol=2e-8)

    def test_invalid_configuration_and_activity(self):
        for modify in (
            lambda c: c.update(max_dimension=2),
            lambda c: c["subsystems"][0].update(initial_populations=[1, 1]),
            lambda c: c.update(couplings=[dict(targets=[0, 2], strength_rad_ns=0.1)]),
            lambda c: c["waveforms"].append(deepcopy(c["waveforms"][0])),
        ):
            config = model(2)
            modify(config)
            with self.assertRaises(ValueError):
                QutipBackend(config)
        backend = QutipBackend(model())
        backend.reset(1, 1)
        with self.assertRaisesRegex(ValueError, "outside activity"):
            backend.execute(1, [evolve(0, 21, [pulse()])])
        with self.assertRaisesRegex(ValueError, "not ideal gates"):
            backend.validate(dict(kind="gate", operation="x", targets=[0]))
        np.testing.assert_array_equal(backend.density_matrix(), [[1, 0], [0, 0]])

    def test_native_bridge_splits_pulse_and_forwards_acquisition(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            config = model(2)
            config["waveforms"][0].update(shape="gaussian", sigma_ns=20)
            config["measurements"] = str(directory / "measurements.jsonl")
            config["readout"] = [
                dict(
                    target=0,
                    means=[[-1, 0], [1, 0]],
                    ringup_ns=5,
                    noise_std_sqrt_ns=0,
                    sample_interval_ns=10,
                    rotation_rad=0,
                    threshold=0,
                )
            ]
            program = directory / "program.S"
            program.write_text(
                '.include "quantum.inc"\n.text\n.global _start\n_start:\n'
                "wait.i 5\ncw.i.i 0, 1\nwait.i 10\ncw.i.i 1, 1\n"
                "wait.i 40\ncw.i.i 0, 2\nwait.i 5\ncw.i.i 1, 1\nfmr a0, 0\nsim_exit\n"
            )
            subprocess.run(
                [
                    args.assembler,
                    "-march=rv32i",
                    "-mabi=ilp32",
                    "-I",
                    str(ROOT / "examples/common"),
                    str(program),
                    "-o",
                    str(directory / "program.o"),
                ],
                check=True,
            )
            subprocess.run(
                [
                    args.linker,
                    "-m",
                    "elf32lriscv",
                    "-T",
                    str(ROOT / "examples/common/link.ld"),
                    str(directory / "program.o"),
                    "-o",
                    str(directory / "program.elf"),
                ],
                check=True,
            )
            mappings = []
            for port, duration, amplitude in ((0, 100, 0.04), (1, 20, 0)):
                mappings.append(
                    dict(
                        port=port,
                        codeword=1,
                        actions=[
                            dict(
                                kind="pulse",
                                operation="drive",
                                port=port,
                                targets=[port],
                                duration=duration,
                                amplitude=amplitude,
                                axis="x",
                            )
                        ],
                    )
                )
            mappings.append(
                dict(
                    port=0,
                    codeword=2,
                    actions=[
                        dict(kind="acquire", operation="measure", port=0, targets=[0], duration=30)
                    ],
                )
            )
            run = dict(
                schema=1,
                program="program.elf",
                backend="qutip",
                backend_options=config,
                profile=dict(
                    start=1000,
                    tcu=dict(period=2, phase=0),
                    qubits=2,
                    two_qubit_gates=[],
                    mappings=mappings,
                ),
                summary="summary.json",
                trace="trace.jsonl",
            )
            path = directory / "run.json"
            path.write_text(json.dumps(run))
            diagnostics = Path(config["measurements"])
            for existing in (False, True):
                if existing:
                    diagnostics.write_text("previous measurement records\n")
                checked = subprocess.run(
                    [str(args.simulator.resolve()), "--config", str(path), "--check-config"],
                    capture_output=True,
                    text=True,
                    timeout=45,
                )
                self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
                self.assertEqual(diagnostics.exists(), existing)
                if existing:
                    self.assertEqual(diagnostics.read_text(), "previous measurement records\n")
                self.assertFalse((directory / "summary.json").exists())
                self.assertFalse((directory / "trace.jsonl").exists())
            result = subprocess.run(
                [str(args.simulator.resolve()), "--config", str(path)],
                capture_output=True,
                text=True,
                timeout=45,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            summary = json.loads((directory / "summary.json").read_text())
            self.assertTrue(summary["success"])
            record = json.loads(Path(config["measurements"]).read_text())
            theta = 0.04 * 20 * math.sqrt(2 * math.pi) * math.erf(2.5 / math.sqrt(2))
            self.assertAlmostEqual(record["probabilities"][1], math.sin(theta / 2) ** 2, places=7)
            self.assertEqual(record["sample_times_ns"], [1120, 1130, 1140])


unittest.main(argv=[sys.argv[0]])
