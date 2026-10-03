"""Backend numerical results and execution-batch partition invariance."""

import argparse
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

parser = argparse.ArgumentParser()
parser.add_argument("--pulse", action="store_true")
parser.add_argument("--aer", action="store_true")
parser.add_argument("--stim", action="store_true")
args = parser.parse_args()
from copy import deepcopy
from qsbit_backend import registry
from backend_test_utils import apply as execute_gates, evolve, measure
if args.aer or args.pulse:
    import numpy as np
    from qiskit import QuantumCircuit
    from qiskit.quantum_info import Statevector
    from qsbit_backend.aer import AerBackend

THERMAL = {"method": "density_matrix", "noise": {"model": "thermal_relaxation", "qubits": [
    {"qubit": 0, "t1_ns": 100, "t2_ns": 200, "excited_state_population": 0}]}}


def gate(operation, targets=(0,), amplitude=0):
    return {"kind": "gate", "operation": operation, "targets": list(targets), "amplitude": amplitude}


def apply(gates, tick=0):
    return {"tick": tick, "kind": "apply", "gates": gates}


def measurement(targets, tick=0):
    return {"tick": tick, "kind": "measure",
            "references": [{"epoch": 1, "measurement": i, "target": q}
                           for i, q in enumerate(targets)]}


@unittest.skipUnless(args.aer or args.pulse, "Aer not enabled")
class Aer(unittest.TestCase):
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
            with patch.object(backend.executor.simulator, "run", wraps=backend.executor.simulator.run) as run:
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
                      {"tick": 100, "kind": "evolve", "start": 0, "drives": []},
                      apply([gate("x")], 100),
                      {"tick": 200, "kind": "evolve", "start": 100, "drives": []}]
        for split in (False, True):
            backend = AerBackend(options)
            backend.reset(2, 7)
            for batch in ([operations] if not split else [[operation] for operation in operations]):
                backend.execute(1, batch)
            probability = (1 - math.exp(-1)) * math.exp(-1)
            np.testing.assert_allclose(backend.density_matrix(),
                                       np.diag([0, 0, 1 - probability, probability]), atol=1e-12)
            bits = backend.execute(1, [apply([gate("h")], 200),
                {"tick": 300, "kind": "evolve", "start": 200, "drives": []}, measurement([0, 1], 300)])
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

    def test_coherence_equilibrium_and_unlisted_qubit(self):
        config = deepcopy(THERMAL)
        config["noise"]["qubits"][0]["excited_state_population"] = 0.2
        backend, _ = registry.create("aer", config)
        backend.reset(2, 10)
        execute_gates(backend, [gate("h"), gate("x", (1,))])
        evolve(backend, 0, 100, [])
        rho = backend.density_matrix()
        self.assertAlmostEqual(rho[3][3].real, 0.2 + 0.3 * math.exp(-1), places=12)
        self.assertAlmostEqual(rho[2][3].real, 0.5 * math.exp(-0.5), places=12)
        self.assertAlmostEqual(rho[0][0].real + rho[1][1].real, 0, places=12)

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
            {"tick": 10, "kind": "evolve", "start": 0, "drives": [
                {"kind": "pulse", "operation": "ry", "port": 0, "targets": [0],
                 "axis": "y", "amplitude": math.pi / 20}]},
            apply([gate("z")], 10)])
        np.testing.assert_allclose(backend.state(), [-1 / math.sqrt(2)] * 2, atol=1e-12)


@unittest.skipUnless(args.stim, "Stim not enabled")
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
                reference.do(circuit)
                targets = ([2, 0], [1], [1, 0, 2])[iteration % 3]
                operations = [{"tick": iteration, "kind": "apply", "gates": [event]}
                              for event in gates]
                operations.append({"tick": iteration, "kind": "measure",
                    "references": [{"epoch": 1, "measurement": i, "target": q}
                                   for i, q in enumerate(targets)]})
                batches = [operations] if iteration % 2 else [[op] for op in operations]
                bits = [bit for batch in batches for bit in backend.execute(1, batch)]
                self.assertEqual(bits, reference.measure_many(*targets))
                self.assertEqual(backend._simulator.current_measurement_record(), [])
            self.assertEqual(backend._simulator.current_inverse_tableau(),
                             reference.current_inverse_tableau())

    def test_bell_collapse_seed_and_reset(self):
        backend, _ = registry.create("stim", {})
        outcomes = []
        for _ in range(2):
            backend.reset(2, 4321)
            execute_gates(backend, [gate("h"), gate("cx", (0, 1))])
            refs = [{"target": 0}, {"target": 1}]
            bits = measure(backend, refs)
            self.assertEqual(bits[0], bits[1])
            self.assertEqual(bits, measure(backend, refs))
            outcomes.append(bits)
        self.assertEqual(*outcomes)

    def test_clifford_rotations_and_rejections(self):
        backend, _ = registry.create("stim", {})
        for operation in ("rx", "ry"):
            backend.reset(1, 5)
            execute_gates(backend, [gate(operation, amplitude=math.pi / 2)] * 2)
            self.assertEqual(measure(backend, [{"target": 0}]), [True])
            execute_gates(backend, [gate(operation, amplitude=-math.pi)])
            self.assertEqual(measure(backend, [{"target": 0}]), [False])
        for event in (gate("t"), gate("rx", amplitude=math.pi / 4), {"kind": "pulse"}):
            with self.assertRaises(ValueError):
                backend.validate(event)
        with self.assertRaises(ValueError):
            evolve(backend, 5, 4, [])

    def test_rotation_signs(self):
        backend, _ = registry.create("stim", {})
        for sign in (-1, 1):
            for operation in ("rx", "ry", "rz"):
                backend.reset(1, 2)
                before = [gate("h")] if operation == "rz" else []
                after = {"rx": [gate("s"), gate("h")], "ry": [gate("h")],
                         "rz": [gate("sdg"), gate("h")]}[operation]
                execute_gates(backend, before + [gate(operation, amplitude=sign * math.pi / 2)] + after)
                self.assertEqual(measure(backend, [{"target": 0}]), [sign < 0])

    def test_depolarization_distribution(self):
        backend, _ = registry.create("stim", {"noise": {
            "model": "depolarizing", "after_gate_probability": 1}})
        backend.reset(4096, 9001)
        execute_gates(backend, [gate("id", (q,)) for q in range(4096)])
        bits = measure(backend, [{"target": q} for q in range(4096)])
        self.assertLess(abs(sum(bits) / len(bits) - 2 / 3), 0.04)

unittest.main(argv=[sys.argv[0]])
