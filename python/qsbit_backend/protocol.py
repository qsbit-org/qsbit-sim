"""Backend API records shared by adapters, recording and replay."""

from typing import Literal, TypedDict, Union

API_VERSION = 4


class Gate(TypedDict):
    kind: Literal["gate"]
    operation: str
    targets: list[int]
    amplitude: float


class Drive(TypedDict):
    kind: Literal["pulse"]
    operation: str
    targets: list[int]
    port: int
    amplitude: float
    axis: Literal["x", "y", "z"]
    id: int
    start: int
    end: int


class Acquisition(TypedDict):
    kind: Literal["acquire"]
    operation: str
    targets: list[int]
    port: int
    id: int
    start: int
    end: int
    measurement: int


class Reference(TypedDict):
    epoch: int
    measurement: int
    target: int


class Evolution(TypedDict):
    kind: Literal["evolve"]
    start: int
    tick: int
    drives: list[Drive]
    acquisitions: list[Acquisition]


class GateApplication(TypedDict):
    kind: Literal["apply"]
    tick: int
    gates: list[Gate]


class Measurement(TypedDict):
    kind: Literal["measure"]
    tick: int
    references: list[Reference]


Operation = Union[Evolution, GateApplication, Measurement]


def validate_batch(epoch: int, operations: list[Operation], qubits: int, validate):
    previous = None
    for index, operation in enumerate(operations):
        kind, tick = operation["kind"], operation["tick"]
        if previous is not None and tick < previous:
            raise ValueError("decreasing operation time")
        if kind == "evolve":
            start = operation["start"]
            if tick < start or (previous is not None and start != previous):
                raise ValueError("invalid evolution interval")
            for drive in operation["drives"]:
                if drive["kind"] != "pulse":
                    raise ValueError("evolution requires pulse drives")
                validate(drive)
            for activity in [*operation["drives"], *operation["acquisitions"]]:
                if not activity["start"] <= start < tick <= activity["end"]:
                    raise ValueError("evolution lies outside activity interval")
            for acquisition in operation["acquisitions"]:
                if acquisition["kind"] != "acquire":
                    raise ValueError("acquisition interval requires acquire event")
                validate(acquisition)
        elif kind == "apply":
            for gate in operation["gates"]:
                if gate["kind"] != "gate":
                    raise ValueError("gate application requires ideal gates")
                validate(gate)
        elif kind == "measure":
            if index != len(operations) - 1:
                raise ValueError("measurement must end the execution batch")
            references = operation["references"]
            targets = [reference["target"] for reference in references]
            if len(set(targets)) != len(targets):
                raise ValueError("ambiguous repeated target in a measurement batch")
            if any(q < 0 or q >= qubits for q in targets):
                raise ValueError("measurement target outside configured register")
            if any(reference["epoch"] != epoch for reference in references):
                raise ValueError("measurement epoch differs from execution batch")
        else:
            raise ValueError("unsupported backend operation: " + kind)
        previous = tick
