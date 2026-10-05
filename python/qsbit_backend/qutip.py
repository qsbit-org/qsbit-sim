"""Time-dependent oscillator Hamiltonians and Lindblad evolution with QuTiP."""

import json
import math
from pathlib import Path

import numpy as np
import qutip as qt

from .protocol import validate_batch
from .registry import options as validate_options


class QutipBackend:
    def __init__(self, options=None):
        self.options = validate_options("qutip", {} if options is None else options)
        self.subsystems = self.options["subsystems"]
        self.levels = [item["levels"] for item in self.subsystems]
        if math.prod(self.levels) > self.options["max_dimension"]:
            raise ValueError("Hilbert-space dimension exceeds backend_options.max_dimension")
        self.waveforms = self._unique(self.options["waveforms"], "operation")
        self.readout = self._unique(self.options["readout"], "target")
        for item in self.subsystems:
            populations = item["initial_populations"]
            if len(populations) != item["levels"] or not math.isclose(sum(populations), 1):
                raise ValueError("initial_populations must contain one normalized entry per level")
        for waveform in self.waveforms.values():
            if waveform["shape"] == "samples":
                times = waveform["times_ns"]
                if len(times) < 2 or times[0] != 0 or any(np.diff(times) <= 0):
                    raise ValueError("sample times must start at zero and strictly increase")
                if len(times) != len(waveform["iq"]):
                    raise ValueError("sample times and IQ values must have equal length")
        for target, channel in self.readout.items():
            if target >= len(self.levels) or len(channel["means"]) != self.levels[target]:
                raise ValueError("readout means must contain one IQ pair per target level")
        self.identity = qt.tensor([qt.qeye(levels) for levels in reversed(self.levels)])
        self.lowering = [self._embed(qt.destroy(n), q) for q, n in enumerate(self.levels)]
        self.numbers = [a.dag() * a for a in self.lowering]
        self.hamiltonian = 0 * self.identity
        self.collapse = []
        for item, a, n in zip(self.subsystems, self.lowering, self.numbers):
            self.hamiltonian += item["detuning_rad_ns"] * n
            self.hamiltonian += item["anharmonicity_rad_ns"] * n * (n - self.identity) / 2
            if item["t1_ns"] is not None:
                self.collapse.append(np.sqrt((1 + item["thermal_occupation"]) / item["t1_ns"]) * a)
                if item["thermal_occupation"]:
                    self.collapse.append(
                        np.sqrt(item["thermal_occupation"] / item["t1_ns"]) * a.dag()
                    )
            if item["tphi_ns"] is not None:
                self.collapse.append(np.sqrt(2 / item["tphi_ns"]) * n)
        for coupling in self.options["couplings"]:
            left, right = coupling["targets"]
            if max(left, right) >= len(self.levels):
                raise ValueError("coupling target outside configured subsystems")
            exchange = self.lowering[left].dag() * self.lowering[right]
            self.hamiltonian += coupling["strength_rad_ns"] * (exchange + exchange.dag())
        solver = self.options["solver"]
        self.solver = {
            "rtol": solver["rtol"],
            "atol": solver["atol"],
            "max_step": solver["max_step_ns"],
            "nsteps": solver["max_steps"],
            "store_final_state": True,
            "store_states": False,
        }
        self.diagnostics = self.options["measurements"]

    @staticmethod
    def _unique(items, key):
        result = {item[key]: item for item in items}
        if len(result) != len(items):
            raise ValueError(f"duplicate {key} in backend options")
        return result

    def _embed(self, operator, target):
        factors = [qt.qeye(levels) for levels in self.levels]
        factors[target] = operator
        return qt.tensor(factors[::-1])

    def reset(self, qubits, seed):
        if qubits != len(self.subsystems):
            raise ValueError("profile.qubits must match the number of QuTiP subsystems")
        self.rng = np.random.default_rng(seed)
        self.data = qt.tensor(
            [qt.Qobj(np.diag(item["initial_populations"])) for item in reversed(self.subsystems)]
        )
        self.acquisitions = {}
        if self.diagnostics:
            Path(self.diagnostics).write_text("", encoding="utf-8")

    def validate(self, action):
        targets = action["targets"]
        if len(targets) != 1 or not 0 <= targets[0] < len(self.levels):
            raise ValueError("QuTiP events require one configured subsystem target")
        if action["kind"] == "pulse":
            if action["operation"] not in self.waveforms:
                raise ValueError(
                    "pulse operation has no configured waveform: " + action["operation"]
                )
            if action["axis"] not in ("x", "y", "z") or not math.isfinite(action["amplitude"]):
                raise ValueError("invalid pulse axis or amplitude")
            waveform = self.waveforms[action["operation"]]
            if "end" in action and waveform["shape"] == "samples":
                if waveform["times_ns"][-1] != action["end"] - action["start"]:
                    raise ValueError("sample waveform must end at the pulse duration")
            if action["axis"] == "z" and (
                waveform["frequency_rad_ns"]
                or waveform["phase_rad"]
                or waveform["shape"] == "drag"
                or (waveform["shape"] == "samples" and any(v[1] for v in waveform["iq"]))
            ):
                raise ValueError(
                    "frequency control requires a real envelope with zero carrier and phase"
                )
        elif action["kind"] not in ("acquire", "arm"):
            raise ValueError("QuTiP accepts pulse and acquisition events, not ideal gates")

    def _envelope(self, drive):
        waveform = self.waveforms[drive["operation"]]
        duration = drive["end"] - drive["start"]

        def coefficient(t):
            elapsed = np.clip(t - drive["start"], 0, duration)
            shape = waveform["shape"]
            if shape == "square":
                iq = 1 + 0j
            elif shape in ("gaussian", "drag"):
                sigma = waveform["sigma_ns"]
                offset = elapsed - duration / 2
                gaussian = np.exp(-0.5 * (offset / sigma) ** 2)
                iq = gaussian + 0j
                if shape == "drag":
                    iq += 1j * waveform["beta_ns"] * (-offset / sigma**2) * gaussian
            else:
                samples = np.asarray(waveform["iq"])
                iq = np.interp(elapsed, waveform["times_ns"], samples[:, 0])
                iq += 1j * np.interp(elapsed, waveform["times_ns"], samples[:, 1])
            phase = waveform["phase_rad"] - waveform["frequency_rad_ns"] * t
            if drive["axis"] == "y":
                phase += np.pi / 2
            return drive["amplitude"] * iq * np.exp(1j * phase)

        return coefficient

    def _evolve(self, operation):
        terms = [self.hamiltonian]
        for drive in operation["drives"]:
            target = drive["targets"][0]
            envelope = self._envelope(drive)
            if drive["axis"] == "z":
                terms.append([self.numbers[target], self._component(envelope, "real")])
            else:
                a = self.lowering[target]
                terms.extend(
                    [
                        [(a + a.dag()) / 2, self._component(envelope, "real")],
                        [1j * (a.dag() - a) / 2, self._component(envelope, "imag")],
                    ]
                )
        for acquisition in operation["acquisitions"]:
            self.acquisitions[acquisition["measurement"]] = acquisition
        if operation["tick"] != operation["start"]:
            self.data = qt.mesolve(
                terms,
                self.data,
                [operation["start"], operation["tick"]],
                self.collapse,
                options=self.solver,
            ).final_state

    @staticmethod
    def _component(envelope, component):
        def coefficient(t):
            return float(getattr(envelope(t), component))

        return coefficient

    def _readout(self, target, level, acquisition):
        if target not in self.readout:
            return bool(level), {}
        channel = self.readout[target]
        duration = acquisition["end"] - acquisition["start"]
        count = math.ceil(duration / channel["sample_interval_ns"])
        edges = np.linspace(0, duration, count + 1)
        width = duration / count
        tau = channel["ringup_ns"]
        response = 1 - tau / width * (np.exp(-edges[:-1] / tau) - np.exp(-edges[1:] / tau))
        mean = complex(*channel["means"][level])
        noise = self.rng.normal(size=(count, 2)) * channel["noise_std_sqrt_ns"] / np.sqrt(width)
        samples = mean * response + noise[:, 0] + 1j * noise[:, 1]
        integrated = samples.mean()
        value = (integrated * np.exp(-1j * channel["rotation_rad"])).real > channel["threshold"]
        return bool(value), {
            "iq": [float(integrated.real), float(integrated.imag)],
            "sample_times_ns": (acquisition["start"] + edges[1:]).tolist(),
            "samples": np.column_stack((samples.real, samples.imag)).tolist(),
        }

    def _measure(self, operation):
        values = []
        for reference in operation["references"]:
            target = reference["target"]
            acquisition = self.acquisitions.get(reference["measurement"])
            if acquisition is not None and (
                acquisition["end"] != operation["tick"] or acquisition["targets"] != [target]
            ):
                raise ValueError("measurement does not match its acquisition interval")
            if target in self.readout and acquisition is None:
                raise ValueError(
                    "configured readout requires an acquisition ending at measurement time"
                )
            probabilities = np.real(self.data.ptrace(len(self.levels) - 1 - target).diag())
            if np.min(probabilities) < -10 * (self.solver["atol"] + self.solver["rtol"]):
                raise ValueError("negative measurement probability exceeds solver tolerance")
            probabilities = np.clip(probabilities, 0, 1)
            probabilities /= probabilities.sum()
            level = int(self.rng.choice(self.levels[target], p=probabilities))
            projector = self._embed(qt.basis(self.levels[target], level).proj(), target)
            projected = projector * self.data * projector
            self.data = projected / projected.tr()
            self.acquisitions.pop(reference["measurement"], None)
            value, readout = self._readout(target, level, acquisition)
            values.append(value)
            if self.diagnostics:
                record = dict(
                    tick=operation["tick"],
                    **reference,
                    probabilities=probabilities.tolist(),
                    level=level,
                    value=value,
                    **readout,
                )
                with Path(self.diagnostics).open("a", encoding="utf-8") as output:
                    output.write(json.dumps(record) + "\n")
        return values

    def execute(self, epoch, operations):
        validate_batch(epoch, operations, len(self.levels), self.validate)
        values = []
        for operation in operations:
            if operation["kind"] == "evolve":
                self._evolve(operation)
            elif operation["kind"] == "measure":
                values.extend(self._measure(operation))
        return values

    def density_matrix(self):
        return self.data.full().tolist()
