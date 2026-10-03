"""Aer batching, observation boundaries and pulse integration."""

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


class Batching(unittest.TestCase):
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
                for tick, event in enumerate(gates):
                    backend.evolve(tick, tick + 1, [])
                    backend.apply([event])
                self.assertEqual(run.call_count, 0)
                if method == "statevector":
                    np.testing.assert_allclose(backend.state(), expected, atol=1e-12)
                else:
                    np.testing.assert_allclose(backend.density_matrix(),
                                               np.outer(expected, expected.conj()), atol=1e-12)
                self.assertEqual(run.call_count, 1)
                backend.state()
                backend.density_matrix()
                self.assertEqual(run.call_count, 1)

    def test_limit_bounds_work_without_observation(self):
        backend = AerBackend({"max_batch_operations": 7})
        backend.reset(1, 9)
        with patch.object(backend._simulator, "run", wraps=backend._simulator.run) as run:
            backend.apply([gate("x")] * 22)
            self.assertEqual(run.call_count, 3)
            np.testing.assert_allclose(backend.state(), [1, 0], atol=1e-12)
            self.assertEqual(run.call_count, 4)

    def test_measurement_seed_independent_of_batching_and_inspection(self):
        for method in ("statevector", "density_matrix"):
            outcomes = []
            for limit, inspect in ((1, False), (7, False), (1024, False), (1024, True)):
                backend = AerBackend({"method": method, "max_batch_operations": limit})
                backend.reset(2, 123)
                observed = []
                for _ in range(12):
                    backend.apply([gate("h"), gate("cx", (0, 1))])
                    if inspect:
                        backend.state()
                        backend.density_matrix()
                    refs = [{"target": 1}, {"target": 0}]
                    bits = backend.measure(refs)
                    self.assertEqual(bits, backend.measure(refs))
                    observed.extend(bits)
                    backend.apply([gate("x", (q,)) for q, bit in zip((1, 0), bits) if bit])
                outcomes.append(observed)
            for observed in outcomes[1:]:
                self.assertEqual(observed, outcomes[0])

    def test_noise_order_and_interval_end(self):
        options = {"method": "density_matrix", "noise": {
            "model": "thermal_relaxation", "qubits": [
                {"qubit": 0, "t1_ns": 100, "t2_ns": 200, "excited_state_population": 0}]}}
        for limit in (1, 1024):
            backend = AerBackend(dict(options, max_batch_operations=limit))
            backend.reset(2, 7)
            backend.apply([gate("x"), gate("x", (1,))])
            backend.evolve(0, 100, [])
            backend.apply([gate("x")])
            backend.evolve(100, 200, [])
            probability = (1 - math.exp(-1)) * math.exp(-1)
            np.testing.assert_allclose(backend.density_matrix(),
                                       np.diag([0, 0, 1 - probability, probability]), atol=1e-12)
            backend.apply([gate("h")])
            backend.evolve(200, 300, [])
            refs = [{"target": 0}, {"target": 1}]
            bits = backend.measure(refs)
            if limit == 1:
                expected_bits = bits
                expected_state = backend.density_matrix()
            else:
                self.assertEqual(bits, expected_bits)
                np.testing.assert_allclose(backend.density_matrix(), expected_state, atol=1e-12)

    def test_reset_validation_and_owned_gate_data(self):
        backend = AerBackend()
        backend.reset(1, 1)
        event = gate("x")
        backend.apply([event])
        event["targets"][0] = 9
        np.testing.assert_allclose(backend.state(), [0, 1], atol=1e-12)
        backend.apply([gate("h")])
        backend.reset(1, 1)
        for invalid in (gate("cx", (0, 0)), gate("x", (1,)), gate("rx", amplitude=math.inf)):
            with self.assertRaises(ValueError):
                backend.apply([gate("x"), invalid])
        backend.apply([gate("x")])
        for refs in ([{"target": 0}, {"target": 0}], [{"target": 1}]):
            with self.assertRaises(ValueError):
                backend.measure(refs)
        self.assertEqual(backend.measure([]), [])
        self.assertEqual(backend.measure([{"target": 0}]), [True])

    def test_transition_probabilities_flush_each_basis(self):
        backend = AerBackend()
        self.assertEqual(backend.transition_probabilities([
            {"method": "apply", "args": [[gate("x")]]}]), [1, 0])


@unittest.skipUnless(args.pulse, "pulse backend not enabled")
class Pulse(unittest.TestCase):
    def test_gates_before_and_after_drive(self):
        from qsbit_backend.pulse import PulseBackend
        for limit in (1, 1024):
            backend = PulseBackend({"max_batch_operations": limit})
            backend.reset(1, 5)
            backend.apply([gate("x")])
            backend.evolve(0, 10, [{"kind": "pulse", "port": 0, "targets": [0],
                                    "axis": "y", "amplitude": math.pi / 20}])
            backend.apply([gate("z")])
            np.testing.assert_allclose(backend.state(), [-1 / math.sqrt(2)] * 2, atol=1e-12)


unittest.main(argv=[sys.argv[0]])
