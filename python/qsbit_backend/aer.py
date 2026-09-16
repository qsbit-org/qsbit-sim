"""Persistent quantum state with real Aer execution at each architectural boundary."""

import numpy as np
from qiskit import QuantumCircuit
from qiskit_aer import AerSimulator


class AerBackend:
    supports_pulse = False
    _arity = {"id": 1, "x": 1, "y": 1, "z": 1, "h": 1, "s": 1,
              "sdg": 1, "t": 1, "tdg": 1, "rx": 1, "ry": 1, "rz": 1,
              "cx": 2, "cz": 2, "swap": 2}

    def __init__(self):
        self._simulator = AerSimulator(method="statevector", max_parallel_threads=1)
        self._state = None
        self._qubits = 0
        self._seed = 0
        self._calls = 0

    def validate(self, action):
        kind = action["kind"]
        if kind == "pulse":
            if not self.supports_pulse:
                raise ValueError("Aer circuit adapter does not integrate pulse drives")
        elif kind == "gate":
            if self._arity.get(action["operation"]) != len(action["targets"]):
                raise ValueError("unsupported ideal gate or target arity")
        elif kind not in ("acquire", "arm"):
            raise ValueError("unsupported action kind")

    def reset(self, qubits, seed):
        if not 0 < qubits <= 20:
            raise ValueError("this statevector adapter supports 1 to 20 qubits")
        self._qubits = qubits
        self._seed = seed
        self._calls = 0
        self._state = np.zeros(1 << qubits, dtype=complex)
        self._state[0] = 1

    def evolve(self, start, end, drives):
        if end < start:
            raise ValueError("decreasing backend time")
        if drives:
            raise ValueError("pulse drives are unsupported by the Aer circuit adapter")

    def _circuit(self, classical=0):
        circuit = QuantumCircuit(self._qubits, classical)
        circuit.set_statevector(self._state)
        return circuit

    def _run(self, circuit, memory=False):
        circuit.save_statevector()
        result = self._simulator.run(
            circuit, shots=1, memory=memory,
            seed_simulator=(self._seed + self._calls) & 0xFFFFFFFF,
        ).result()
        if not result.success:
            raise RuntimeError(result.status)
        self._state = np.asarray(result.get_statevector(), dtype=complex)
        self._calls += 1
        return result

    def apply(self, gates):
        if not gates:
            return
        for gate in gates:
            self.validate(gate)
        circuit = self._circuit()
        for gate in gates:
            operation = gate["operation"]
            args = gate["targets"]
            if operation in ("rx", "ry", "rz"):
                args = [gate["amplitude"], *args]
            getattr(circuit, operation)(*args)
        self._run(circuit)

    def measure(self, tokens):
        if not tokens:
            return []
        targets = [token["target"] for token in tokens]
        if len(set(targets)) != len(targets):
            raise ValueError("ambiguous repeated target in a measurement batch")
        circuit = self._circuit(len(tokens))
        for bit, target in enumerate(targets):
            circuit.measure(target, bit)
        result = self._run(circuit, memory=True)
        bits = result.get_memory()[0].replace(" ", "")[::-1]
        return [bit == "1" for bit in bits]

    def state(self):
        return self._state.tolist()
