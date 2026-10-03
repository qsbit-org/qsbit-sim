"""Validate the ordered operation batch supplied by the simulator."""


def validate_batch(backend, epoch, operations):
    previous = None
    for index, operation in enumerate(operations):
        method, args, tick = operation["method"], operation["args"], operation["tick"]
        if previous is not None and tick < previous:
            raise ValueError("decreasing operation time")
        if method == "evolve":
            start, end, drives = args
            if end != tick or end < start or (previous is not None and start != previous):
                raise ValueError("invalid evolution interval")
            for drive in drives:
                if drive["kind"] != "pulse":
                    raise ValueError("evolution requires pulse drives")
                backend.validate(drive)
        elif method == "apply":
            for gate in args[0]:
                if gate["kind"] != "gate":
                    raise ValueError("gate application requires ideal gates")
                backend.validate(gate)
        elif method == "measure":
            if index != len(operations) - 1:
                raise ValueError("measurement must end the execution batch")
            targets = [reference["target"] for reference in args[0]]
            if len(set(targets)) != len(targets):
                raise ValueError("ambiguous repeated target in a measurement batch")
            if any(q < 0 or q >= backend._qubits for q in targets):
                raise ValueError("measurement target outside configured register")
            if any(reference["epoch"] != epoch for reference in args[0]):
                raise ValueError("measurement epoch differs from execution batch")
        else:
            raise ValueError("unsupported backend operation: " + method)
        previous = tick
