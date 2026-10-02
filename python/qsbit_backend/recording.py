"""Record backend calls for repeated-program simulation."""

import json
from pathlib import Path

from .registry import create


class RecordingBackend:
    @staticmethod
    def describe():
        return {"api_version": 1, "options_schema": {"type": "object", "properties": {
            "backend": {"type": "string"}, "options": {"type": "object"},
            "output": {"type": "string"}, "outcome": {"type": ["boolean", "null"]}},
            "required": ["backend", "options", "output", "outcome"], "additionalProperties": False}}

    def __init__(self, options):
        if options["backend"] == "qsbit_backend.recording:RecordingBackend":
            raise ValueError("recursive recording backend")
        self.backend, _ = create(options["backend"], options["options"])
        self.output = Path(options["output"])
        self.outcome = options["outcome"]
        self.calls = []
        self.tick = 0

    def validate(self, action):
        self.backend.validate(action)

    def reset(self, qubits, seed):
        if self.calls:
            raise ValueError("recording does not support session resets")
        self.backend.reset(qubits, seed)

    def evolve(self, start, end, drives):
        self.tick = end
        self.calls.append({"tick": end, "method": "evolve", "args": [start, end, drives]})
        if self.outcome is None:
            self.backend.evolve(start, end, drives)

    def apply(self, gates):
        if gates:
            self.calls.append({"tick": self.tick, "method": "apply", "args": [gates]})
            if self.outcome is None:
                self.backend.apply(gates)

    def measure(self, references):
        if not references:
            return []
        bits = (self.backend.measure(references) if self.outcome is None
                else [self.outcome] * len(references))
        self.calls.append({"tick": self.tick, "method": "measure", "args": [references], "bits": bits})
        return bits

    def state(self):
        self.output.write_text(json.dumps(self.calls))
        return getattr(self.backend, "state", lambda: [])() if self.outcome is None else []

    def density_matrix(self):
        return getattr(self.backend, "density_matrix", lambda: [])() if self.outcome is None else []
