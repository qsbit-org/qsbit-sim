"""Execute ordered quantum operation batches with Aer."""

import numpy as np
from qiskit_aer.noise import thermal_relaxation_error

from .circuit import CircuitExecutor, validate_gate
from .protocol import validate_batch
from .registry import options as validate_options


class AerBackend:
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
        self.executor = CircuitExecutor(self._method, int(self.options["max_parallel_threads"]))

    def validate(self, action):
        if action["kind"] == "gate":
            validate_gate(action, self.executor.qubits)
        elif action["kind"] not in ("acquire", "arm"):
            raise ValueError("Aer circuit adapter does not integrate pulse drives")

    def reset(self, qubits, seed):
        limit = 10 if self._method == "density_matrix" else 20
        if not 0 < qubits <= limit:
            raise ValueError(f"{self._method} adapter supports 1 to {limit} qubits")
        for index, item in enumerate(self._noise.get("qubits", [])):
            if item["qubit"] >= qubits:
                raise ValueError(
                    f"backend_options.noise.qubits[{index}].qubit: outside configured register"
                )
        self.executor.reset(qubits, seed)

    def _evolve(self, circuit, start, end, drives):
        if drives:
            raise ValueError("pulse drives are unsupported by the Aer circuit adapter")
        if end == start or self._noise["model"] == "none":
            return
        for item in self._noise["qubits"]:
            channel = thermal_relaxation_error(
                item["t1_ns"], item["t2_ns"], end - start, item["excited_state_population"]
            )
            circuit.append(channel.to_instruction(), [int(item["qubit"])])

    def execute(self, epoch, operations):
        validate_batch(epoch, operations, self.executor.qubits, self.validate)
        return self.executor.execute(operations, self._evolve)

    def state(self):
        return self.executor.state()

    def density_matrix(self):
        return self.executor.density_matrix()

    def transition_probabilities(self, operations):
        result = []
        for basis in (0, 1):
            self.reset(1, 0)
            self.executor.data[:] = 0
            if self._method == "density_matrix":
                self.executor.data[basis, basis] = 1
            else:
                self.executor.data[basis] = 1
            for operation in operations:
                if operation["kind"] not in ("evolve", "apply"):
                    raise ValueError("transition segment accepts only evolution and gates")
            self.execute(1, operations)
            probability = (
                self.executor.data[1, 1].real
                if self._method == "density_matrix"
                else abs(self.executor.data[1]) ** 2
            )
            if not np.isfinite(probability) or not -1e-12 <= probability <= 1 + 1e-12:
                raise ValueError("invalid transition probability")
            result.append(float(np.clip(probability, 0, 1)))
        return result
