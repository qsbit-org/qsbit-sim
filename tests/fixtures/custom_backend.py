"""Dependency-free external backend for adapter contract tests."""

from qsbit_backend.protocol import API_VERSION
import json
from pathlib import Path

class Backend:
    @staticmethod
    def describe():
        return {"api_version": API_VERSION, "options_schema": {
            "type": "object", "additionalProperties": False}, "requirements": [],
            "capabilities": {"state_outputs": []}}

    def __init__(self, options):
        self.outcome = True

    def validate(self, action):
        if action["kind"] not in ("gate", "acquire", "arm"):
            raise ValueError("unsupported action")

    def reset(self, qubits, seed):
        self.qubits = qubits

    def execute(self, epoch, operations):
        for operation in operations:
            if operation["kind"] == "evolve":
                start, end, drives = operation["start"], operation["tick"], operation["drives"]
                assert end >= start and not drives
            elif operation["kind"] == "apply":
                for gate in operation["gates"]:
                    self.validate(gate)
            else:
                return [self.outcome for _ in operation["references"]]
        return []

    def state(self):
        return []


class Incomplete:
    describe = Backend.describe

    def __init__(self, options):
        pass


class Configured(Backend):
    @staticmethod
    def describe():
        return {"api_version": API_VERSION, "options_schema": {
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
