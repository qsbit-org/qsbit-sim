"""Record operation batches for repeated-program simulation."""

import json
from pathlib import Path

from .protocol import API_VERSION
from .registry import create


class RecordingBackend:
    @staticmethod
    def describe():
        return {
            "api_version": API_VERSION,
            "options_schema": {
                "type": "object",
                "properties": {
                    "backend": {"type": "string"},
                    "options": {"type": "object"},
                    "output": {"type": "string"},
                    "outcome": {"type": ["boolean", "null"]},
                },
                "required": ["backend", "options", "output", "outcome"],
                "additionalProperties": False,
            },
        }

    def __init__(self, options):
        if options["backend"] == "qsbit_backend.recording:RecordingBackend":
            raise ValueError("recursive recording backend")
        self.backend, _ = create(options["backend"], options["options"])
        self.output = Path(options["output"])
        self.outcome = options["outcome"]
        self.calls = []
        self.batches = 0

    def validate(self, action):
        self.backend.validate(action)

    def reset(self, qubits, seed):
        if self.calls:
            raise ValueError("recording does not support session resets")
        self.backend.reset(qubits, seed)

    def execute(self, epoch, operations):
        references = operations[-1]["references"] if operations[-1]["kind"] == "measure" else []
        bits = (
            self.backend.execute(epoch, operations)
            if self.outcome is None
            else [self.outcome] * len(references)
        )
        for operation in operations:
            record = dict(operation, batch=self.batches)
            if operation["kind"] == "measure":
                record["bits"] = bits
            self.calls.append(record)
        self.batches += 1
        return bits

    def state(self):
        self.output.write_text(json.dumps(self.calls))
        return getattr(self.backend, "state", lambda: [])() if self.outcome is None else []

    def density_matrix(self):
        return getattr(self.backend, "density_matrix", lambda: [])() if self.outcome is None else []
