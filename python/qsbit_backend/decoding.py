"""Validate decoder configurations and construct numerical decoder callbacks."""

import importlib

import jsonschema


def _integer(minimum=1, maximum=2**32 - 1):
    return {"type": "integer", "minimum": minimum, "maximum": maximum}


SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "properties": {
        "mmio_base": _integer(0),
        "request_capacity": _integer(),
        "result_capacity": _integer(),
        "link_latency": _integer(),
        "bytes_per_tick": _integer(),
        "packet_overhead": _integer(0),
        "decoders": {
            "type": "array",
            "minItems": 1,
            "items": {
                "type": "object",
                "additionalProperties": False,
                "properties": {
                    "id": _integer(0),
                    "measurements": _integer(1, 1048576),
                    "outputs": _integer(1, 64),
                    "latency": _integer(),
                    "initiation_interval": _integer(),
                    "backend": {"type": "string", "minLength": 1},
                    "options": {"type": "object"},
                },
                "required": [
                    "id",
                    "measurements",
                    "outputs",
                    "latency",
                    "initiation_interval",
                    "backend",
                    "options",
                ],
            },
        },
    },
    "required": [
        "mmio_base",
        "request_capacity",
        "result_capacity",
        "link_latency",
        "bytes_per_tick",
        "packet_overhead",
        "decoders",
    ],
}


def validate(config):
    jsonschema.validate(config, SCHEMA)
    if config["mmio_base"] % 4 or config["mmio_base"] > 2**32 - 32:
        raise ValueError("decoder MMIO must be an aligned 32-byte range")
    ids = [d["id"] for d in config["decoders"]]
    if len(set(ids)) != len(ids):
        raise ValueError("decoder ids must be unique")
    return config


def create(config):
    name = config["backend"]
    if name != "pymatching":
        module, separator, attribute = name.partition(":")
        if not separator:
            raise ValueError("decoder backend must be pymatching or MODULE:FACTORY")
        return _checked(getattr(importlib.import_module(module), attribute)(config), config)
    import numpy as np
    import pymatching

    options = config["options"]
    if set(options) != {"check_matrix", "observables", "measurement_to_detector", "weights"}:
        raise ValueError(
            "pymatching options require check_matrix, observables, measurement_to_detector, weights"
        )

    def binary_matrix(name):
        value = np.asarray(options[name])
        if value.ndim != 2 or not np.isin(value, [0, 1]).all():
            raise ValueError(f"{name} must be a binary matrix")
        return value.astype(np.uint8)

    checks = binary_matrix("check_matrix")
    observables = binary_matrix("observables")
    detectors = binary_matrix("measurement_to_detector")
    if detectors.shape != (checks.shape[0], config["measurements"]):
        raise ValueError("measurement_to_detector shape does not match decoder inputs")
    if observables.shape != (config["outputs"], checks.shape[1]):
        raise ValueError("observables shape does not match decoder outputs")
    weights = np.asarray(options["weights"], dtype=float)
    if weights.shape != (checks.shape[1],) or not np.isfinite(weights).all():
        raise ValueError("weights must contain one finite value per error mechanism")
    matching = pymatching.Matching(checks, weights=weights, faults_matrix=observables)

    def decode(bits):
        result = matching.decode((detectors @ np.asarray(bits, dtype=np.uint8)) % 2)
        return [bool(bit) for bit in result]

    return _checked(decode, config)


def _checked(decode, config):
    if not callable(decode):
        raise ValueError("decoder factory must return a callable")

    def checked(bits):
        if len(bits) != config["measurements"] or any(bit not in (0, 1) for bit in bits):
            raise ValueError("decoder input must match the configured binary measurement window")
        result = list(decode(bits))
        if len(result) != config["outputs"] or any(bit not in (0, 1) for bit in result):
            raise ValueError("decoder output must match the configured binary result width")
        return [bool(bit) for bit in result]

    return checked
