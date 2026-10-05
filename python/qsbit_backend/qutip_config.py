"""Configuration schema for driven, dissipative oscillator models."""

from .catalog import obj


def schema():
    number = {"type": "number"}
    positive = {"type": "number", "exclusiveMinimum": 0}
    nonnegative = {"type": "number", "minimum": 0}
    index = {"type": "integer", "minimum": 0}
    lifetime = {"anyOf": [positive, {"type": "null"}]}

    def vector(item):
        return {"type": "array", "minItems": 1, "items": item}

    pair = dict(vector(number), minItems=2, maxItems=2)
    subsystem = obj(
        {
            "levels": {"type": "integer", "minimum": 2},
            "detuning_rad_ns": number,
            "anharmonicity_rad_ns": number,
            "t1_ns": lifetime,
            "tphi_ns": lifetime,
            "thermal_occupation": nonnegative,
            "initial_populations": vector(nonnegative),
        },
        (
            "levels",
            "detuning_rad_ns",
            "anharmonicity_rad_ns",
            "t1_ns",
            "tphi_ns",
            "thermal_occupation",
            "initial_populations",
        ),
    )
    base = {
        "operation": {"type": "string", "minLength": 1},
        "phase_rad": number,
        "frequency_rad_ns": number,
    }
    waveform = {
        "oneOf": [
            obj(dict(base, shape={"const": "square"}), (*base, "shape")),
            obj(
                dict(base, shape={"const": "gaussian"}, sigma_ns=positive),
                (*base, "shape", "sigma_ns"),
            ),
            obj(
                dict(base, shape={"const": "drag"}, sigma_ns=positive, beta_ns=number),
                (*base, "shape", "sigma_ns", "beta_ns"),
            ),
            obj(
                dict(
                    base, shape={"const": "samples"}, times_ns=vector(nonnegative), iq=vector(pair)
                ),
                (*base, "shape", "times_ns", "iq"),
            ),
        ]
    }
    coupling = obj(
        {
            "targets": dict(vector(index), minItems=2, maxItems=2, uniqueItems=True),
            "strength_rad_ns": number,
        },
        ("targets", "strength_rad_ns"),
    )
    readout = obj(
        {
            "target": index,
            "means": vector(pair),
            "ringup_ns": positive,
            "noise_std_sqrt_ns": nonnegative,
            "sample_interval_ns": positive,
            "rotation_rad": number,
            "threshold": number,
        },
        (
            "target",
            "means",
            "ringup_ns",
            "noise_std_sqrt_ns",
            "sample_interval_ns",
            "rotation_rad",
            "threshold",
        ),
    )
    return obj(
        {
            "subsystems": vector(subsystem),
            "couplings": dict(vector(coupling), minItems=0, default=[]),
            "waveforms": dict(vector(waveform), minItems=0, default=[]),
            "readout": dict(vector(readout), minItems=0, default=[]),
            "solver": obj(
                {
                    "rtol": positive,
                    "atol": positive,
                    "max_step_ns": positive,
                    "max_steps": {"type": "integer", "minimum": 1},
                },
                ("rtol", "atol", "max_step_ns", "max_steps"),
            ),
            "max_dimension": {"type": "integer", "minimum": 2},
            "measurements": {
                "type": ["string", "null"],
                "default": None,
                "description": "Optional JSONL measurement diagnostics path.",
            },
        },
        ("subsystems", "solver", "max_dimension"),
    )
