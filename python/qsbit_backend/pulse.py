"""Small-system piecewise-constant Hamiltonian integration, in radians per ns."""

import numpy as np
from scipy.linalg import expm
from .circuit import CircuitExecutor, validate_gate
from .protocol import validate_batch
from .registry import options as validate_options


class PulseBackend:

    def __init__(self, options=None):
        self.options = validate_options("pulse", {} if options is None else options)
        self.executor = CircuitExecutor("statevector", int(self.options["max_parallel_threads"]))

    def reset(self, qubits, seed):
        if not 0 < qubits <= 8:
            raise ValueError("dense pulse prototype supports at most 8 qubits")
        self.executor.reset(qubits, seed)

    def validate(self, action):
        if action["kind"] == "gate":
            validate_gate(action, self.executor.qubits)
        elif action["kind"] == "pulse":
            targets = action["targets"]
            if (len(targets) != 1 or action["axis"] not in ("x", "y", "z")
                    or not np.isfinite(action["amplitude"])
                    or not 0 <= targets[0] < self.executor.qubits):
                raise ValueError("unsupported pulse operator")
        elif action["kind"] not in ("acquire", "arm"):
            raise ValueError("unsupported action kind")

    def execute(self, epoch, operations):
        validate_batch(epoch, operations, self.executor.qubits, self.validate)
        return self.executor.execute(operations, self._evolve)

    def state(self):
        return self.executor.state()

    def _evolve(self, circuit, start, end, drives):
        if end < start:
            raise ValueError("decreasing backend time")
        if end == start or not drives:
            return
        paulis = {
            "x": np.array([[0, 1], [1, 0]], dtype=complex),
            "y": np.array([[0, -1j], [1j, 0]], dtype=complex),
            "z": np.array([[1, 0], [0, -1]], dtype=complex),
        }
        hamiltonian = np.zeros((1 << self.executor.qubits, 1 << self.executor.qubits), dtype=complex)
        for drive in sorted(drives, key=lambda a: (a["port"], a["targets"], a["axis"])):
            operator = np.array([[1]], dtype=complex)
            for qubit in reversed(range(self.executor.qubits)):
                local = paulis[drive["axis"]] if qubit == drive["targets"][0] else np.eye(2)
                operator = np.kron(operator, local)
            hamiltonian += 0.5 * drive["amplitude"] * operator
        circuit.unitary(expm(-1j * (end - start) * hamiltonian), list(range(self.executor.qubits)))
