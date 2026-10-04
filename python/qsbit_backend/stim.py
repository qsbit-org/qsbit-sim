"""Persistent Clifford simulation with Stim."""

import math

import stim

from .protocol import validate_batch
from .registry import options as validate_options


class StimBackend:
    _gates = {
        "id": "I",
        "x": "X",
        "y": "Y",
        "z": "Z",
        "h": "H",
        "s": "S",
        "sdg": "S_DAG",
        "cx": "CX",
        "cz": "CZ",
        "swap": "SWAP",
    }
    _rotations = {"rx": "SQRT_X", "ry": "SQRT_Y", "rz": "S"}

    def __init__(self, options=None):
        self.options = validate_options("stim", {} if options is None else options)
        self._simulator = None

    def reset(self, qubits, seed):
        if qubits < 1:
            raise ValueError("qubits must be positive")
        self._simulator = stim.TableauSimulator(seed=seed)
        self._simulator.set_num_qubits(qubits)
        self._qubits = qubits

    def validate(self, action):
        kind = action["kind"]
        if kind in ("acquire", "arm"):
            return
        if kind != "gate":
            raise ValueError("Stim does not support pulse drives")
        operation = action["operation"]
        if operation not in self._gates and operation not in self._rotations:
            raise ValueError(f"Stim does not support gate {operation}")
        targets = action["targets"]
        arity = 2 if operation in ("cx", "cz", "swap") else 1
        if len(targets) != arity or len(set(targets)) != arity:
            raise ValueError("invalid gate target arity")
        if operation in self._rotations:
            angle = action["amplitude"]
            if not math.isfinite(angle) or not math.isclose(
                angle, round(angle / (math.pi / 2)) * (math.pi / 2), rel_tol=0, abs_tol=1e-12
            ):
                raise ValueError("Stim rotations require integer multiples of pi/2")

    def execute(self, epoch, operations):
        validate_batch(epoch, operations, self._qubits, self.validate)
        circuit = stim.Circuit()
        noise = self.options["noise"]
        references = []
        for item in operations:
            if item["kind"] == "measure":
                references = item["references"]
            elif item["kind"] == "apply":
                for gate in item["gates"]:
                    operation, targets = gate["operation"], gate["targets"]
                    if operation in self._rotations:
                        for _ in range(round(gate["amplitude"] / (math.pi / 2)) % 4):
                            circuit.append(self._rotations[operation], targets)
                    else:
                        circuit.append(self._gates[operation], targets)
                    if noise["model"] == "depolarizing":
                        circuit.append("DEPOLARIZE1", targets, noise["after_gate_probability"])
        self._simulator.do(circuit)
        targets = [item["target"] for item in references]
        if not targets:
            return []
        sampled = self._simulator.copy(copy_rng=True)
        bits = sampled.measure_many(*targets)
        # Reset consumes the same collapse randomness without recording outcomes.
        self._simulator.reset(*targets)
        self._simulator.set_inverse_tableau(sampled.current_inverse_tableau())
        return bits
