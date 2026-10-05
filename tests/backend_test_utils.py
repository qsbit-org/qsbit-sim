"""Construct explicit execution batches for adapter tests."""


def apply(backend, gates):
    return backend.execute(1, [{"tick": 0, "kind": "apply", "gates": gates}])


def evolve(backend, start, end, drives):
    return backend.execute(
        1, [{"tick": end, "kind": "evolve", "start": start, "drives": drives, "acquisitions": []}]
    )


def measure(backend, references):
    references = [
        dict(reference, epoch=1, measurement=index) for index, reference in enumerate(references)
    ]
    return backend.execute(1, [{"tick": 0, "kind": "measure", "references": references}])
