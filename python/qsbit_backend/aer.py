"""Persistent quantum state with real Aer execution at each architectural boundary."""

import numpy as np
from qiskit import QuantumCircuit
from qiskit_aer import AerSimulator
from qiskit_aer.noise import thermal_relaxation_error

from .registry import options as validate_options


class AerBackend:
    supports_pulse = False
    _arity = {"id": 1, "x": 1, "y": 1, "z": 1, "h": 1, "s": 1,
              "sdg": 1, "t": 1, "tdg": 1, "rx": 1, "ry": 1, "rz": 1,
              "cx": 2, "cz": 2, "swap": 2}

    def __init__(self, options=None):
        self.options = validate_options("aer", {} if options is None else options)
        self._method = self.options["method"]
        self._noise = self.options["noise"]
        if self._noise["model"] != "none" and self._method != "density_matrix":
            raise ValueError("backend_options.method: thermal_relaxation requires density_matrix")
        seen = set()
        for index, item in enumerate(self._noise.get("qubits", [])):
            path = f"backend_options.noise.qubits[{index}]"
            if item["qubit"] in seen:
                raise ValueError(f"{path}.qubit: duplicate target")
            seen.add(item["qubit"])
            if item["t2_ns"] > 2 * item["t1_ns"]:
                raise ValueError(f"{path}.t2_ns: must be at most twice t1_ns")
        self._simulator = AerSimulator(method=self._method,
            max_parallel_threads=int(self.options["max_parallel_threads"]))
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
        limit = 10 if self._method == "density_matrix" else 20
        if not 0 < qubits <= limit:
            raise ValueError(f"{self._method} adapter supports 1 to {limit} qubits")
        for index, item in enumerate(self._noise.get("qubits", [])):
            if item["qubit"] >= qubits:
                raise ValueError(f"backend_options.noise.qubits[{index}].qubit: outside configured register")
        self._qubits = qubits
        self._seed = seed
        self._calls = 0
        self._state = np.zeros(1 << qubits, dtype=complex)
        self._state[0] = 1
        if self._method == "density_matrix":
            self._state = np.outer(self._state, self._state.conj())

    def evolve(self, start, end, drives):
        if end < start:
            raise ValueError("decreasing backend time")
        if drives:
            raise ValueError("pulse drives are unsupported by the Aer circuit adapter")
        if end == start or self._noise["model"] == "none":
            return
        circuit = self._circuit()
        for item in self._noise["qubits"]:
            channel = thermal_relaxation_error(item["t1_ns"], item["t2_ns"], end - start,
                                               item["excited_state_population"])
            circuit.append(channel.to_instruction(), [int(item["qubit"])])
        self._run(circuit)

    def _circuit(self, classical=0):
        circuit = QuantumCircuit(self._qubits, classical)
        getattr(circuit, "set_" + self._method)(self._state)
        return circuit

    def _run(self, circuit, memory=False):
        getattr(circuit, "save_" + self._method)()
        result = self._simulator.run(
            circuit, shots=1, memory=memory,
            seed_simulator=(self._seed + self._calls) & 0xFFFFFFFF,
        ).result()
        if not result.success:
            raise RuntimeError(result.status)
        self._state = np.asarray(result.data(0)[self._method], dtype=complex)
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

    def measure(self, references):
        if not references:
            return []
        targets = [reference["target"] for reference in references]
        if len(set(targets)) != len(targets):
            raise ValueError("ambiguous repeated target in a measurement batch")
        circuit = self._circuit(len(references))
        for bit, target in enumerate(targets):
            circuit.measure(target, bit)
        result = self._run(circuit, memory=True)
        bits = result.get_memory()[0].replace(" ", "")[::-1]
        return [bit == "1" for bit in bits]

    def state(self):
        return self._state.tolist() if self._method == "statevector" else []

    def density_matrix(self):
        return self._state.tolist() if self._method == "density_matrix" else []
