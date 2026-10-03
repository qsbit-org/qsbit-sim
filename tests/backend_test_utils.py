"""Construct explicit execution batches for adapter tests."""


def apply(backend, gates):
    return backend.execute(1, [{"tick": 0, "method": "apply", "args": [gates]}])


def evolve(backend, start, end, drives):
    return backend.execute(1, [{"tick": end, "method": "evolve", "args": [start, end, drives]}])


def measure(backend, references):
    references = [dict(reference, epoch=1) for reference in references]
    return backend.execute(1, [{"tick": 0, "method": "measure", "args": [references]}])
