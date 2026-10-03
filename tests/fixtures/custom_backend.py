"""Dependency-free external backend for adapter contract tests."""

import json
from pathlib import Path

class Backend:
    def validate(self, action):
        if action["kind"] not in ("gate", "acquire", "arm"):
            raise ValueError("unsupported action")

    def reset(self, qubits, seed):
        self.qubits = qubits

    def execute(self, epoch, operations):
        for operation in operations:
            if operation["method"] == "evolve":
                start, end, drives = operation["args"]
                assert end >= start and not drives
            elif operation["method"] == "apply":
                for gate in operation["args"][0]:
                    self.validate(gate)
            else:
                return [getattr(self, "outcome", True) for _ in operation["args"][0]]
        return []

    def state(self):
        return []


class Incomplete:
    pass


class Configured(Backend):
    @staticmethod
    def describe():
        return {"api_version": 2, "options_schema": {
            "type": "object", "$defs": {"bit": {"type": "boolean"}},
            "properties": {"outcome": {"$ref": "#/$defs/bit"}, "audit_path": {"type": "string"}},
            "required": ["outcome"], "additionalProperties": False}, "requirements": [],
            "capabilities": {"state_outputs": []}}

    def __init__(self, options):
        self.outcome = options["outcome"]
        self.audit_path = options.get("audit_path")
        self.batches = []

    def execute(self, epoch, operations):
        self.batches.append({"epoch": epoch, "operations": operations})
        return super().execute(epoch, operations)

    def state(self):
        if self.audit_path:
            Path(self.audit_path).write_text(json.dumps(self.batches))
        return []
