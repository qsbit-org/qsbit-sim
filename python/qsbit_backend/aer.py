"""Execute ordered quantum operation batches with Aer."""

import numpy as np
from qiskit import ClassicalRegister, QuantumCircuit
from qiskit_aer import AerSimulator
from qiskit_aer.noise import thermal_relaxation_error

from .registry import options as validate_options
from .batch import validate_batch


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
        self._measurements = 0

    def validate(self, action):
        kind = action["kind"]
        if kind == "pulse":
            if not self.supports_pulse:
                raise ValueError("Aer circuit adapter does not integrate pulse drives")
        elif kind == "gate":
            targets = action["targets"]
            if (self._arity.get(action["operation"]) != len(targets)
                    or len(set(targets)) != len(targets)):
                raise ValueError("unsupported ideal gate or target arity")
            if self._qubits and any(q < 0 or q >= self._qubits for q in targets):
                raise ValueError("gate target outside configured register")
            if action["operation"] in ("rx", "ry", "rz") and not np.isfinite(action["amplitude"]):
                raise ValueError("rotation angle must be finite")
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
        self._measurements = 0
        self._state = np.zeros(1 << qubits, dtype=complex)
        self._state[0] = 1
        if self._method == "density_matrix":
            self._state = np.outer(self._state, self._state.conj())

    def _evolve(self, circuit, start, end, drives):
        if drives:
            raise ValueError("pulse drives are unsupported by the Aer circuit adapter")
        if end == start or self._noise["model"] == "none":
            return
        for item in self._noise["qubits"]:
            channel = thermal_relaxation_error(item["t1_ns"], item["t2_ns"], end - start,
                                               item["excited_state_population"])
            circuit.append(channel.to_instruction(), [int(item["qubit"])])

    def execute(self, epoch, operations):
        validate_batch(self, epoch, operations)
        circuit = QuantumCircuit(self._qubits)
        getattr(circuit, "set_" + self._method)(self._state)
        measured = False
        for operation in operations:
            method, args = operation["method"], operation["args"]
            if method == "evolve":
                self._evolve(circuit, *args)
            elif method == "apply":
                for gate in args[0]:
                    operands = gate["targets"]
                    if gate["operation"] in ("rx", "ry", "rz"):
                        operands = [gate["amplitude"], *operands]
                    getattr(circuit, gate["operation"])(*operands)
            elif args[0]:
                circuit.add_register(ClassicalRegister(len(args[0])))
                for bit, reference in enumerate(args[0]):
                    circuit.measure(reference["target"], bit)
                measured = True
        if len(circuit.data) == 1:
            return []
        getattr(circuit, "save_" + self._method)()
        result = self._simulator.run(circuit, shots=1, memory=measured,
            seed_simulator=(self._seed + self._measurements) & 0xFFFFFFFF).result()
        if not result.success:
            raise RuntimeError(result.status)
        self._state = np.asarray(result.data(0)[self._method], dtype=complex)
        if not measured:
            return []
        self._measurements += 1
        return [bit == "1" for bit in result.get_memory()[0].replace(" ", "")[::-1]]

    def state(self):
        return self._state.tolist() if self._method == "statevector" else []

    def density_matrix(self):
        return self._state.tolist() if self._method == "density_matrix" else []

    def transition_probabilities(self, operations):
        if self.supports_pulse:
            raise ValueError("transition probabilities require the circuit adapter")
        result = []
        for basis in (0, 1):
            self.reset(1, 0)
            self._state[:] = 0
            if self._method == "density_matrix":
                self._state[basis, basis] = 1
            else:
                self._state[basis] = 1
            for operation in operations:
                if operation["method"] not in ("evolve", "apply"):
                    raise ValueError("transition segment accepts only evolution and gates")
            self.execute(1, operations)
            probability = (self._state[1, 1].real if self._method == "density_matrix"
                           else abs(self._state[1]) ** 2)
            if not np.isfinite(probability) or not -1e-12 <= probability <= 1 + 1e-12:
                raise ValueError("invalid transition probability")
            result.append(float(np.clip(probability, 0, 1)))
        return result
