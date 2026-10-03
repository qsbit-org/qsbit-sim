"""Backend descriptions without imports of numerical packages."""


def obj(properties, required=()):
    return {"type": "object", "properties": properties,
            "required": list(required), "additionalProperties": False}


NONE = obj({"model": {"const": "none"}}, ("model",))
PROBABILITY = {"type": "number", "minimum": 0, "maximum": 1}
THERMAL = obj({
    "model": {"const": "thermal_relaxation"},
    "qubits": {"type": "array", "minItems": 1, "items": obj({
        "qubit": {"type": "integer", "minimum": 0},
        "t1_ns": {"type": "number", "exclusiveMinimum": 0,
                  "description": "Energy relaxation time in nanoseconds."},
        "t2_ns": {"type": "number", "exclusiveMinimum": 0,
                  "description": "Coherence decay time in nanoseconds; at most twice T1."},
        "excited_state_population": dict(PROBABILITY,
            description="Equilibrium probability of the excited state."),
    }, ("qubit", "t1_ns", "t2_ns", "excited_state_population"))},
}, ("model", "qubits"))
DEPOLARIZING = obj({
    "model": {"const": "depolarizing"},
    "after_gate_probability": dict(PROBABILITY,
        description="Independent single-qubit depolarization on each gate target after each gate."),
}, ("model", "after_gate_probability"))

GATES = ["id", "x", "y", "z", "h", "s", "sdg", "t", "tdg", "rx", "ry", "rz",
         "cx", "cz", "swap"]


def descriptor(factory, extra, requirements, schema, capabilities):
    return {"api_version": 1, "factory": factory,
            "install": f"python -m pip install 'qsbit-sim-backends[{extra}]'",
            "requirements": requirements, "options_schema": schema,
            "capabilities": capabilities}


def builtins():
    aer = obj({
        "method": {"enum": ["statevector", "density_matrix"], "default": "statevector",
                   "description": "Thermal relaxation requires density_matrix."},
        "max_parallel_threads": {"type": "integer", "minimum": 1, "default": 1},
        "max_batch_operations": {"type": "integer", "minimum": 1, "default": 1024,
                                 "description": "Maximum queued gates and single-qubit noise channels before Aer execution."},
        "noise": {"oneOf": [NONE, THERMAL], "default": {"model": "none"},
                  "description": "Applied over every elapsed interval, including acquisition."},
    })
    pulse = obj({key: aer["properties"][key] for key in ("max_parallel_threads", "max_batch_operations")})
    stim = obj({"noise": {"oneOf": [NONE, DEPOLARIZING], "default": {"model": "none"}}})
    return {
        "aer": descriptor("qsbit_backend.aer:AerBackend", "aer",
            ["qiskit", "qiskit-aer", "numpy"], aer,
            {"gates": GATES, "pulse": False, "state_outputs": ["statevector", "density_matrix"],
             "qubit_limits": {"statevector": 20, "density_matrix": 10},
             "transition_probabilities": "Single-qubit computational-basis projective measurements."}),
        "pulse": descriptor("qsbit_backend.pulse:PulseBackend", "pulse",
            ["qiskit", "qiskit-aer", "numpy", "scipy"], pulse,
            {"gates": GATES, "pulse": True, "state_outputs": ["statevector"], "max_qubits": 8}),
        "stim": descriptor("qsbit_backend.stim:StimBackend", "stim", ["stim"], stim,
            {"gates": [g for g in GATES if g not in ("t", "tdg")], "pulse": False,
             "rotations": "rx, ry and rz require integer multiples of pi/2 (absolute tolerance 1e-12 radians).",
             "state_outputs": []}),
    }
