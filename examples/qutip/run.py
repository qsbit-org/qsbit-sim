"""Assemble qsbit pulse-control experiments, run qsbit-sim and plot QuTiP results."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
from importlib import metadata
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import curve_fit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXPERIMENTS = ("rabi", "ramsey", "t1", "echo", "allxy", "reset", "readout", "coupler")


class Program:
    def __init__(self, settings, options):
        self.settings = settings
        self.options = options
        self.instructions = []
        self.mappings = []
        self.next_codeword = 1

    def wait(self, duration):
        if duration:
            if duration % self.settings["tcu_period_ns"]:
                raise ValueError("wait duration must be a multiple of the TCU period")
            self.instructions.extend(
                [f"li t0, {int(duration // self.settings['tcu_period_ns'])}", "wait.r t0"]
            )

    def emit(self, kind, target=0, **fields):
        codeword = self.next_codeword
        self.next_codeword += 1
        action = dict(kind=kind, port=target, targets=[target], **fields)
        self.mappings.append(dict(port=target, codeword=codeword, actions=[action]))
        self.instructions.extend([f"li t1, {codeword}", f"cw.i.r {target}, t1"])

    def pulse(self, amplitude, duration=None, axis="x", target=0, operation="drive"):
        duration = self.settings["pulse_ns"] if duration is None else int(duration)
        if duration:
            self.emit(
                "pulse",
                target,
                operation=operation,
                duration=duration,
                amplitude=float(amplitude),
                axis=axis,
            )
            self.wait(duration)

    def measure(self, duration=None):
        self.emit(
            "acquire",
            operation="measure",
            duration=self.settings["acquisition_ns"] if duration is None else duration,
            discriminator_delay=self.settings["discriminator_delay_ns"],
        )
        self.instructions.append("fmr a1, 0")

    def gate(self, name, scale=1):
        if name == "I":
            self.wait(self.settings["pulse_ns"])
        else:
            amplitude = self.settings["pi_amplitude_rad_ns"] * scale
            self.pulse(amplitude if name.isupper() else amplitude / 2, axis=name.lower())

    def assembly(self):
        return (
            ' .include "quantum.inc"\n.text\n.global _start\n_start:\n'
            + "\n".join(self.instructions)
            + "\nsim_exit\n"
        )


def run_case(task, args):
    name, value, program, seed = task
    directory = args.output / name
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "program.S").write_text(program.assembly())
    commands = [
        [
            args.assembler,
            "-march=rv32i",
            "-mabi=ilp32",
            "-mno-relax",
            "-I",
            str(ROOT / "examples/common"),
            "program.S",
            "-o",
            "program.o",
        ],
        [
            args.linker,
            "-m",
            "elf32lriscv",
            "--no-relax",
            "-T",
            str(ROOT / "examples/common/link.ld"),
            "program.o",
            "-o",
            "program.elf",
        ],
    ]
    options = deepcopy(program.options)
    options["measurements"] = str(directory / "measurements.jsonl")
    config = dict(
        schema=1,
        program="program.elf",
        backend="qutip",
        backend_options=options,
        profile=dict(
            start=program.settings["controller_start_ns"],
            tcu=dict(period=program.settings["tcu_period_ns"], phase=0),
            qubits=len(options["subsystems"]),
            seed=seed,
            two_qubit_gates=[],
            mappings=program.mappings,
        ),
        trace="trace.jsonl",
        summary="summary.json",
    )
    (directory / "run.json").write_text(json.dumps(config, indent=2) + "\n")
    commands.append([str(args.simulator), "--config", "run.json"])
    with (directory / "run.log").open("w") as log:
        for command in commands:
            result = subprocess.run(
                command,
                cwd=directory,
                stdout=log,
                stderr=log,
                timeout=120,
                env=dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1"),
            )
            if result.returncode:
                raise RuntimeError(f"{name} failed; see {directory / 'run.log'}")
    summary = json.loads((directory / "summary.json").read_text())
    if not summary["success"]:
        raise RuntimeError(f"{name}: {summary}")
    measurements = [
        json.loads(line) for line in (directory / "measurements.jsonl").read_text().splitlines()
    ]
    result = dict(
        name=name, parameter=value, stop_tick=summary["stop_tick"], measurements=measurements
    )
    if not measurements:
        encoded = np.asarray(summary["density_matrix"])
        rho = encoded[:, :, 0] + 1j * encoded[:, :, 1]
        levels = [item["levels"] for item in options["subsystems"]]
        populations = np.diag(rho).real.reshape(levels[::-1])
        result["populations"] = [
            populations.sum(
                axis=tuple(i for i in range(len(levels)) if i != len(levels) - 1 - q)
            ).tolist()
            for q in range(len(levels))
        ]
    return result


def cases(settings, base, selected):
    tasks = []

    def add(experiment, value, build, options=None):
        number = sum(task[0].startswith(experiment + "-") for task in tasks)
        program = Program(settings, deepcopy(base if options is None else options))
        build(program)
        tasks.append((f"{experiment}-{number:03}", value, program, settings["seed"] + number))

    def scan(key):
        return np.linspace(*settings[key], dtype=int).tolist()

    amplitude = settings["pi_amplitude_rad_ns"]
    if "rabi" in selected:
        for duration in scan("rabi_duration_ns"):
            add("rabi", duration, lambda p: (p.pulse(amplitude, duration), p.measure()))
    for experiment in ("ramsey", "echo"):
        if experiment not in selected:
            continue
        options = deepcopy(base)
        options["subsystems"][0].update(
            detuning_rad_ns=settings["ramsey_detuning_rad_ns"], tphi_ns=settings["tphi_ns"]
        )
        for delay in scan(f"{experiment}_delay_ns"):

            def sequence(p):
                p.pulse(amplitude / 2)
                if experiment == "echo":
                    p.wait(delay // 2)
                    p.pulse(amplitude)
                    p.wait(delay - delay // 2)
                else:
                    p.wait(delay)
                p.pulse(amplitude / 2)
                p.measure()

            add(experiment, delay, sequence, options)
    if "t1" in selected:
        options = deepcopy(base)
        options["subsystems"][0]["t1_ns"] = settings["t1_ns"]
        for delay in scan("t1_delay_ns"):
            add("t1", delay, lambda p: (p.pulse(amplitude), p.wait(delay), p.measure()), options)
    if "allxy" in selected:
        for scale in settings["allxy_amplitude_scales"]:
            for pair in settings["allxy_pairs"]:
                add(
                    "allxy",
                    dict(pair=pair, scale=scale),
                    lambda p: (p.gate(pair[0], scale), p.gate(pair[1], scale), p.measure()),
                )
    if "reset" in selected:
        for shot in range(settings["reset_shots"]):

            def sequence(p):
                p.pulse(amplitude / 2)
                p.measure()
                p.wait(settings["feedback_budget_ns"])
                p.instructions.append("beq a1, zero, skip_reset")
                p.emit(
                    "pulse",
                    operation="drive",
                    duration=settings["pulse_ns"],
                    amplitude=amplitude,
                    axis="x",
                )
                p.instructions.append("skip_reset:")
                p.wait(settings["pulse_ns"])
                p.measure()

            add("reset", shot, sequence)
    if "readout" in selected:
        options = deepcopy(base)
        options["readout"] = [settings["readout"]]
        for state in (0, 1):
            for shot in range(settings["readout_shots_per_state"]):
                add(
                    "readout",
                    dict(prepared=state, shot=shot),
                    lambda p: (
                        p.pulse(amplitude * state),
                        p.measure(settings["readout_duration_ns"]),
                    ),
                    options,
                )
    if "coupler" in selected:
        for duration in scan("coupler_duration_ns"):
            options = deepcopy(base)
            options.update(
                subsystems=settings["coupler_subsystems"], couplings=settings["couplings"]
            )
            if duration:
                ramp = min(settings["coupler_ramp_ns"], duration / 2)
                times = sorted(set([0, ramp, duration - ramp, duration]))
                options["waveforms"][1].update(
                    shape="samples",
                    times_ns=times,
                    iq=[[0 if t in (0, duration) else 1, 0] for t in times],
                )

            def sequence(p):
                p.pulse(amplitude)
                p.pulse(
                    settings["coupler_frequency_shift_rad_ns"],
                    duration,
                    axis="z",
                    target=2,
                    operation="flux",
                )

            add("coupler", duration, sequence, options)
    return tasks


def plot(results, settings, directory):
    directory.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update(
        {
            "font.size": 11,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "axes.grid": True,
            "grid.alpha": 0.18,
            "figure.dpi": 160,
        }
    )
    metrics = {}

    def group(name):
        return [result for result in results if result["name"].startswith(name + "-")]

    def save(fig, name):
        fig.tight_layout()
        fig.savefig(directory / f"{name}.png", facecolor="white")
        plt.close(fig)

    for name in ("rabi", "ramsey", "t1", "echo"):
        data = group(name)
        if not data:
            continue
        x = np.array([r["parameter"] for r in data])
        y = np.array([r["measurements"][0]["probabilities"][1] for r in data])
        fig, ax = plt.subplots(figsize=(7, 4))
        ax.plot(x, y, "o", ms=4, label="QuTiP pulse evolution", color="#176b91")
        if name == "rabi":
            reference = np.sin(settings["pi_amplitude_rad_ns"] * x / 2) ** 2
            metrics["rabi_max_error"] = float(np.max(abs(y - reference)))
            ax.plot(x, reference, "-", color="#d28030", label="sin²(Ωt/2)")
        elif name == "t1":
            fit, _ = curve_fit(
                lambda t, a, lifetime: a * np.exp(-t / lifetime), x, y, p0=[y[0], settings["t1_ns"]]
            )
            metrics["fitted_t1_ns"] = float(fit[1])
            ax.plot(
                x, fit[0] * np.exp(-x / fit[1]), color="#d28030", label=f"Fit: T₁ = {fit[1]:.2f} ns"
            )
        elif name == "ramsey":

            def fringe(t, contrast, frequency, phase, lifetime, offset):
                return offset + contrast * np.exp(-t / lifetime) * np.cos(frequency * t + phase)

            fit, _ = curve_fit(
                fringe,
                x,
                y,
                p0=[0.5, settings["ramsey_detuning_rad_ns"], 0, settings["tphi_ns"], 0.5],
                maxfev=20000,
            )
            metrics["fitted_ramsey_detuning_rad_ns"] = float(fit[1])
            dense = np.linspace(x[0], x[-1], 1000)
            ax.plot(dense, fringe(dense, *fit), color="#d28030", label="Damped sinusoid fit")
        else:
            fit, _ = curve_fit(
                lambda t, a, lifetime, offset: offset - a * np.exp(-t / lifetime),
                x,
                y,
                p0=[0.5, settings["tphi_ns"], 0.5],
            )
            metrics["fitted_echo_decay_ns"] = float(fit[1])
            ax.plot(
                x,
                fit[2] - fit[0] * np.exp(-x / fit[1]),
                color="#d28030",
                label=f"Fit: decay = {fit[1]:.2f} ns",
            )
        ax.set(
            title={
                "rabi": "Rabi oscillation",
                "ramsey": "Ramsey fringes",
                "t1": "Energy relaxation",
                "echo": "Hahn echo",
            }[name],
            xlabel="Pulse duration (ns)" if name == "rabi" else "Free-evolution delay (ns)",
            ylabel="Excited-state probability at acquisition end",
            ylim=(-0.04, 1.04),
        )
        ax.legend()
        save(fig, name)
    data = group("allxy")
    if data:
        fig, ax = plt.subplots(figsize=(10, 4))
        pairs = settings["allxy_pairs"]
        for scale in settings["allxy_amplitude_scales"]:
            y = [
                r["measurements"][0]["probabilities"][1]
                for r in data
                if r["parameter"]["scale"] == scale
            ]
            ax.plot(range(len(pairs)), y, "o-", label=f"Amplitude scale {scale:g}")
            if scale == 1:
                metrics["allxy_max_error"] = float(
                    np.max(abs(np.array(y) - ([0] * 5 + [0.5] * 12 + [1] * 4)))
                )
        ax.set(
            title="AllXY: coherent amplitude error",
            ylabel="Excited-state probability",
            xticks=range(len(pairs)),
            xticklabels=pairs,
            ylim=(-0.04, 1.04),
        )
        ax.legend()
        save(fig, "allxy")
    data = group("reset")
    if data:
        before = [r["measurements"][0]["value"] for r in data]
        after = [r["measurements"][1]["value"] for r in data]
        metrics["reset_shots"] = len(data)
        metrics["reset_ones_before"] = sum(before)
        metrics["reset_ones_after"] = sum(after)
        fig, axes = plt.subplots(1, 2, figsize=(9, 3.7))
        axes[0].bar(
            ["Before reset", "After reset"],
            [np.mean(before), np.mean(after)],
            color=["#d28030", "#176b91"],
        )
        axes[0].set(
            ylabel="Measured fraction of ones",
            ylim=(0, 1),
            title=f"Active reset: {len(data)} shots",
        )
        axes[1].plot(before, ".", label="First measurement", color="#d28030")
        axes[1].plot(after, "+", label="Verification", color="#176b91")
        axes[1].set(xlabel="Shot", ylabel="Returned bit", yticks=[0, 1], ylim=(-0.1, 1.3))
        axes[1].legend()
        save(fig, "reset")
    data = group("readout")
    if data:
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        errors = 0
        for state, color in ((0, "#176b91"), (1, "#d28030")):
            records = [r["measurements"][0] for r in data if r["parameter"]["prepared"] == state]
            iq = np.array([r["iq"] for r in records])
            axes[0].scatter(
                iq[:, 0], iq[:, 1], s=18, alpha=0.7, label=f"Prepared |{state}⟩", color=color
            )
            record = records[0]
            times = np.array(record["sample_times_ns"]) - record["sample_times_ns"][0]
            axes[1].plot(
                times,
                np.array(record["samples"])[:, 0],
                alpha=0.7,
                color=color,
                label=f"Prepared |{state}⟩",
            )
            errors += sum(r["value"] != bool(state) for r in records)
        metrics["readout_assignment_errors"] = errors
        metrics["readout_shots"] = len(data)
        axes[0].axvline(settings["readout"]["threshold"], color="#555555", linestyle="--")
        axes[0].set(title="Integrated IQ samples", xlabel="I", ylabel="Q")
        axes[1].set(
            title="Synthetic readout records", xlabel="Time from first sample (ns)", ylabel="I"
        )
        for ax in axes:
            ax.legend()
        save(fig, "readout")
    data = group("coupler")
    if data:
        fig, ax = plt.subplots(figsize=(8, 4))
        x = [r["parameter"] for r in data]
        for q, label in enumerate(("Qubit 0", "Qubit 1", "Coupler")):
            y = [1 - r["populations"][q][0] for r in data]
            ax.plot(x, y, "o-", ms=3, label=label)
        metrics["coupler_max_transfer"] = max(1 - r["populations"][1][0] for r in data)
        metrics["coupler_max_excitation"] = max(1 - r["populations"][2][0] for r in data)
        ax.set(
            title="Exchange through a flux-controlled coupler",
            xlabel="Coupler pulse duration (ns)",
            ylabel="Probability outside the ground level",
            ylim=(-0.04, 1.04),
        )
        ax.legend()
        save(fig, "coupler")
    tolerance = settings["validation"]
    for key in ("rabi_max_error", "allxy_max_error"):
        if key in metrics and metrics[key] > tolerance["probability_tolerance"]:
            raise RuntimeError(f"{key} exceeds configured tolerance: {metrics[key]}")
    for key, expected in (
        ("fitted_t1_ns", settings["t1_ns"]),
        ("fitted_ramsey_detuning_rad_ns", settings["ramsey_detuning_rad_ns"]),
        ("fitted_echo_decay_ns", settings["tphi_ns"]),
    ):
        if (
            key in metrics
            and abs(metrics[key] / expected - 1) > tolerance["fit_relative_tolerance"]
        ):
            raise RuntimeError(f"{key} differs from the configured model: {metrics[key]}")
    if metrics.get("reset_ones_after", 0):
        raise RuntimeError("ideal active reset returned an excited-state measurement")
    return metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", type=Path, default=ROOT / "build-clang/qsbit-sim")
    parser.add_argument(
        "--assembler", default=shutil.which("riscv64-unknown-elf-as") or "riscv64-elf-as"
    )
    parser.add_argument(
        "--linker", default=shutil.which("riscv64-unknown-elf-ld") or "riscv64-elf-ld"
    )
    parser.add_argument("--model", type=Path, default=HERE / "model.json")
    parser.add_argument("--settings", type=Path, default=HERE / "experiments.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/qutip")
    parser.add_argument("--figures", type=Path, default=ROOT / "build/qutip/figs")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--only", nargs="+", choices=EXPERIMENTS, default=EXPERIMENTS)
    args = parser.parse_args()
    args.simulator, args.output = args.simulator.resolve(), args.output.resolve()
    settings = json.loads(args.settings.read_text())
    tasks = cases(settings, json.loads(args.model.read_text()), args.only)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = []
        for result in pool.map(lambda task: run_case(task, args), tasks):
            results.append(result)
            print(f"[{len(results)}/{len(tasks)}] {result['name']}", flush=True)
    metrics = plot(results, settings, args.figures)
    report = dict(
        metrics=metrics,
        settings=settings,
        results=results,
        versions={
            package: metadata.version(package)
            for package in ("qutip", "numpy", "scipy", "matplotlib")
        },
        sha256={
            str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (
                args.model,
                args.settings,
                Path(__file__),
                ROOT / "python/qsbit_backend/qutip.py",
            )
        },
    )
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
