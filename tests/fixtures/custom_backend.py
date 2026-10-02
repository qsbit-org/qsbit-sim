"""Dependency-free external backend for adapter contract tests."""


class Backend:
    def validate(self, action):
        if action["kind"] not in ("gate", "acquire", "arm"):
            raise ValueError("unsupported action")

    def reset(self, qubits, seed):
        self.qubits = qubits

    def evolve(self, start, end, drives):
        assert end >= start and not drives

    def apply(self, gates):
        for gate in gates:
            self.validate(gate)

    def measure(self, references):
        return [True for reference in references]

    def state(self):
        return []


class Incomplete:
    pass
