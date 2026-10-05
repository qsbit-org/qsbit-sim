"""Aer circuit execution for the Aer adapter."""

import numpy as np
from qiskit import ClassicalRegister, QuantumCircuit
from qiskit_aer import AerSimulator

_ARITY = {
    "id": 1,
    "x": 1,
    "y": 1,
    "z": 1,
    "h": 1,
    "s": 1,
    "sdg": 1,
    "t": 1,
    "tdg": 1,
    "rx": 1,
    "ry": 1,
    "rz": 1,
    "cx": 2,
    "cz": 2,
    "swap": 2,
}


def validate_gate(action, qubits):
    targets = action["targets"]
    if _ARITY.get(action["operation"]) != len(targets) or len(set(targets)) != len(targets):
        raise ValueError("unsupported ideal gate or target arity")
    if qubits and any(q < 0 or q >= qubits for q in targets):
        raise ValueError("gate target outside configured register")
    if action["operation"] in ("rx", "ry", "rz") and not np.isfinite(action["amplitude"]):
        raise ValueError("rotation angle must be finite")


class CircuitExecutor:
    def __init__(self, method, threads):
        self.method = method
        self.simulator = AerSimulator(method=method, max_parallel_threads=threads)
        self.qubits = 0
        self.data = None

    def reset(self, qubits, seed):
        self.qubits, self.seed, self.measurements = qubits, seed, 0
        self.data = np.zeros(1 << qubits, dtype=complex)
        self.data[0] = 1
        if self.method == "density_matrix":
            self.data = np.outer(self.data, self.data.conj())

    def execute(self, operations, evolve):
        circuit = QuantumCircuit(self.qubits)
        getattr(circuit, "set_" + self.method)(self.data)
        measured = False
        for operation in operations:
            method = operation["kind"]
            if method == "evolve":
                evolve(circuit, operation["start"], operation["tick"], operation["drives"])
            elif method == "apply":
                for gate in operation["gates"]:
                    operands = gate["targets"]
                    if gate["operation"] in ("rx", "ry", "rz"):
                        operands = [gate["amplitude"], *operands]
                    getattr(circuit, gate["operation"])(*operands)
            elif operation["references"]:
                circuit.add_register(ClassicalRegister(len(operation["references"])))
                for bit, reference in enumerate(operation["references"]):
                    circuit.measure(reference["target"], bit)
                measured = True
        if len(circuit.data) == 1:
            return []
        getattr(circuit, "save_" + self.method)()
        result = self.simulator.run(
            circuit,
            shots=1,
            memory=measured,
            seed_simulator=(self.seed + self.measurements) & 0xFFFFFFFF,
        ).result()
        if not result.success:
            raise RuntimeError(result.status)
        self.data = np.asarray(result.data(0)[self.method], dtype=complex)
        if not measured:
            return []
        self.measurements += 1
        return [bit == "1" for bit in result.get_memory()[0].replace(" ", "")[::-1]]

    def state(self):
        return self.data.tolist() if self.method == "statevector" else []

    def density_matrix(self):
        return self.data.tolist() if self.method == "density_matrix" else []
