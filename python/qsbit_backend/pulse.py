"""Small-system piecewise-constant Hamiltonian integration, in radians per ns."""

import numpy as np
from scipy.linalg import expm
from .aer import AerBackend


class PulseBackend(AerBackend):
    supports_pulse = True

    def reset(self, qubits, seed):
        if qubits > 8:
            raise ValueError("dense pulse prototype supports at most 8 qubits")
        super().reset(qubits, seed)

    def evolve(self, start, end, drives):
        if end < start:
            raise ValueError("decreasing backend time")
        if end == start or not drives:
            return
        paulis = {
            "x": np.array([[0, 1], [1, 0]], dtype=complex),
            "y": np.array([[0, -1j], [1j, 0]], dtype=complex),
            "z": np.array([[1, 0], [0, -1]], dtype=complex),
        }
        hamiltonian = np.zeros((1 << self._qubits, 1 << self._qubits), dtype=complex)
        for drive in sorted(drives, key=lambda a: (a["port"], a["targets"], a["axis"])):
            self.validate(drive)
            if len(drive["targets"]) != 1 or drive["axis"] not in paulis:
                raise ValueError("unsupported pulse operator")
            operator = np.array([[1]], dtype=complex)
            for qubit in reversed(range(self._qubits)):
                local = paulis[drive["axis"]] if qubit == drive["targets"][0] else np.eye(2)
                operator = np.kron(operator, local)
            hamiltonian += 0.5 * drive["amplitude"] * operator
        self._state = expm(-1j * (end - start) * hamiltonian) @ self._state
