"""Numerical results for explicit Aer and pulse execution batches."""

import argparse
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from qsbit_backend.aer import AerBackend

parser = argparse.ArgumentParser()
parser.add_argument("--pulse", action="store_true")
args = parser.parse_args()


def gate(operation, targets=(0,), amplitude=0):
    return {"kind": "gate", "operation": operation, "targets": list(targets), "amplitude": amplitude}


def apply(gates, tick=0):
    return {"tick": tick, "method": "apply", "args": [gates]}


def measurement(targets, tick=0):
    return {"tick": tick, "method": "measure",
            "args": [[{"epoch": 1, "target": q} for q in targets]]}


class Batches(unittest.TestCase):
    def test_gates_match_independent_statevector(self):
        gates = [gate("h"), gate("cx", (0, 2)), gate("s", (1,)), gate("y", (2,)),
                 gate("rx", (1,), 0.37), gate("ry", (0,), -0.61), gate("rz", (2,), 0.29),
                 gate("cz", (2, 1)), gate("swap", (1, 0)), gate("t"), gate("sdg", (2,)),
                 gate("tdg", (1,)), gate("z"), gate("x", (1,)), gate("id", (2,))] * 8
        circuit = QuantumCircuit(3)
        for event in gates:
            operands = event["targets"]
            if event["operation"] in ("rx", "ry", "rz"):
                operands = [event["amplitude"], *operands]
            getattr(circuit, event["operation"])(*operands)
        expected = Statevector.from_instruction(circuit).data
        for method in ("statevector", "density_matrix"):
            backend = AerBackend({"method": method})
            backend.reset(3, 7)
            with patch.object(backend._simulator, "run", wraps=backend._simulator.run) as run:
                backend.execute(1, [apply([event]) for event in gates])
                if method == "statevector":
                    np.testing.assert_allclose(backend.state(), expected, atol=1e-12)
                else:
                    np.testing.assert_allclose(backend.density_matrix(),
                                               np.outer(expected, expected.conj()), atol=1e-12)
                self.assertEqual(run.call_count, 1)

    def test_measurement_seed_independent_of_partition_and_inspection(self):
        for method in ("statevector", "density_matrix"):
            outcomes = []
            for split in (False, True):
                backend = AerBackend({"method": method})
                backend.reset(2, 123)
                observed = []
                for _ in range(12):
                    operations = [apply([gate("h")]), apply([gate("cx", (0, 1))]),
                                  measurement([1, 0])]
                    if split:
                        for operation in operations[:-1]:
                            backend.execute(1, [operation])
                            backend.state()
                            backend.density_matrix()
                        operations = operations[-1:]
                    bits = backend.execute(1, operations)
                    self.assertEqual(bits, backend.execute(1, [measurement([1, 0])]))
                    observed.extend(bits)
                    backend.execute(1, [apply([gate("x", (q,)) for q, bit in zip((1, 0), bits) if bit])])
                outcomes.append(observed)
            self.assertEqual(*outcomes)

    def test_noise_order_and_final_state(self):
        options = {"method": "density_matrix", "noise": {
            "model": "thermal_relaxation", "qubits": [
                {"qubit": 0, "t1_ns": 100, "t2_ns": 200, "excited_state_population": 0}]}}
        operations = [apply([gate("x"), gate("x", (1,))]),
                      {"tick": 100, "method": "evolve", "args": [0, 100, []]},
                      apply([gate("x")], 100),
                      {"tick": 200, "method": "evolve", "args": [100, 200, []]}]
        for split in (False, True):
            backend = AerBackend(options)
            backend.reset(2, 7)
            for batch in ([operations] if not split else [[operation] for operation in operations]):
                backend.execute(1, batch)
            probability = (1 - math.exp(-1)) * math.exp(-1)
            np.testing.assert_allclose(backend.density_matrix(),
                                       np.diag([0, 0, 1 - probability, probability]), atol=1e-12)
            bits = backend.execute(1, [apply([gate("h")], 200),
                {"tick": 300, "method": "evolve", "args": [200, 300, []]}, measurement([0, 1], 300)])
            if not split:
                expected_bits, expected_state = bits, backend.density_matrix()
            else:
                self.assertEqual(bits, expected_bits)
                np.testing.assert_allclose(backend.density_matrix(), expected_state, atol=1e-12)

    def test_invalid_batch_has_no_effect(self):
        backend = AerBackend()
        backend.reset(1, 1)
        for operations in ([apply([gate("x"), gate("cx", (0, 0))])],
                           [apply([gate("x"), gate("rx", amplitude=math.inf)])],
                           [measurement([0]), apply([gate("x")])],
                           [apply([gate("x")]), measurement([0, 0])],
                           [measurement([1])]):
            with self.assertRaises(ValueError):
                backend.execute(1, operations)
            np.testing.assert_allclose(backend.state(), [1, 0], atol=1e-12)
        with self.assertRaisesRegex(ValueError, "epoch"):
            backend.execute(2, [measurement([0])])

    def test_transition_probabilities(self):
        backend = AerBackend()
        self.assertEqual(backend.transition_probabilities([apply([gate("x")])]), [1, 0])


@unittest.skipUnless(args.pulse, "pulse backend not enabled")
class Pulse(unittest.TestCase):
    def test_gates_and_drive_in_one_execution(self):
        from qsbit_backend.pulse import PulseBackend
        backend = PulseBackend()
        backend.reset(1, 5)
        backend.execute(1, [apply([gate("x")]),
            {"tick": 10, "method": "evolve", "args": [0, 10, [
                {"kind": "pulse", "port": 0, "targets": [0], "axis": "y", "amplitude": math.pi / 20}]]},
            apply([gate("z")], 10)])
        np.testing.assert_allclose(backend.state(), [-1 / math.sqrt(2)] * 2, atol=1e-12)


unittest.main(argv=[sys.argv[0]])
