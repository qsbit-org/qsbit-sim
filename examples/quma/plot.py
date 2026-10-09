"""Check AllXY probabilities and timing, then plot the experiment."""

import argparse
import json
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

PAIRS = [
    "II",
    "XX",
    "YY",
    "XY",
    "YX",
    "xI",
    "yI",
    "xy",
    "yx",
    "xY",
    "yX",
    "Xy",
    "Yx",
    "xX",
    "Xx",
    "yY",
    "Yy",
    "XI",
    "YI",
    "xx",
    "yy",
]


def check_control(trace, calls, start):
    operations = {"I": "id", "X": "x", "Y": "y", "x": "rx", "y": "ry"}
    starts = [event for event in trace if event["kind"] == "OperationStart"]
    assert starts and len(starts) % 126 == 0
    for index, event in enumerate(starts):
        trial, operation = divmod(index, 3)
        assert event["tick"] == start + trial * 200040 + 200000 + operation * 20
        expected = "measure" if operation == 2 else operations[PAIRS[(trial % 42) // 2][operation]]
        assert event["operation"] == expected and event["targets"] == [0]
    gates = [gate for call in calls if call["kind"] == "apply" for gate in call["gates"]]
    for index, gate in enumerate(gates):
        trial, position = divmod(index, 2)
        symbol = PAIRS[(trial % 42) // 2][position]
        assert gate["operation"] == operations[symbol] and gate["targets"] == [0]
        if symbol in ("x", "y"):
            assert abs(gate["amplitude"] - math.pi / 2) < 1e-15


def analytic(pair, basis, idle, t1, t2, equilibrium):
    bloch = np.array([0.0, 0.0, 1.0 - 2 * basis])

    def wait(duration):
        bloch[:2] *= math.exp(-duration / t2)
        z_eq = 1 - 2 * equilibrium
        bloch[2] = z_eq + (bloch[2] - z_eq) * math.exp(-duration / t1)

    wait(idle)
    for index, gate in enumerate(pair):
        angle = math.pi if gate.isupper() else math.pi / 2
        x, y, z = bloch
        if gate.lower() == "x":
            bloch[:] = (
                x,
                y * math.cos(angle) - z * math.sin(angle),
                y * math.sin(angle) + z * math.cos(angle),
            )
        elif gate.lower() == "y":
            bloch[:] = (
                x * math.cos(angle) + z * math.sin(angle),
                y,
                -x * math.sin(angle) + z * math.cos(angle),
            )
        wait(20 if index == 0 else 1520)
    return float((1 - bloch[2]) / 2)


def check(result):
    repetitions = result["simulation"]["repetitions"]
    assert result["success"] and result["measurement_count"] == 42 * repetitions
    assert result["timing"]["period_tcu_cycles"] == 42 * 40008
    assert result["timing"]["time_point_ns"] == repetitions * 42 * 40008 * 5
    assert (
        result["timing"]["last_measurement_trigger_ns"]
        == result["profile"]["start"] + result["timing"]["time_point_ns"]
    )
    parameters = result["backend_options"]["noise"]["qubits"][0]
    for mode in ("first", "steady"):
        expected = []
        for slot in range(42):
            idle = 200000 + result["profile"]["start"] if mode == "first" and slot == 0 else 198500
            expected.append(
                [
                    analytic(
                        PAIRS[slot // 2],
                        basis,
                        idle,
                        parameters["t1_ns"],
                        parameters["t2_ns"],
                        parameters["excited_state_population"],
                    )
                    for basis in (0, 1)
                ]
            )
        np.testing.assert_allclose(
            result["transition_probabilities"][mode], expected, atol=2e-12, rtol=0
        )
    p = np.array(result["expected_probabilities"])
    observed = np.array(result["probabilities"])
    tolerance = 6 * np.sqrt(p * (1 - p) / repetitions) + 1 / repetitions
    assert np.all(np.abs(observed - p) <= tolerance), (
        "shot frequencies exceed the six-sigma tolerance"
    )


def plot(result, output):
    check(result)
    output.mkdir(parents=True, exist_ok=True)
    shots = result["simulation"]["repetitions"]
    p = np.array(result["probabilities"])
    model = np.array(result["expected_probabilities"])
    ideal = np.repeat([0] * 5 + [0.5] * 12 + [1] * 4, 2)
    x = np.arange(42)
    zero, one = p[:2].mean(), p[34:38].mean()
    calibrated = (p - zero) / (one - zero)
    model_calibrated = (model - model[:2].mean()) / (model[34:38].mean() - model[:2].mean())
    error = 1.96 * np.sqrt(model * (1 - model) / shots)
    plt.rcParams.update({"font.size": 11, "axes.spines.top": False, "axes.spines.right": False})
    fig, axes = plt.subplots(2, 1, figsize=(14, 8), sharex=True, layout="constrained")
    for ax, data, reference, scale, ylabel in (
        (axes[0], p, model, 1, "Probability of measuring 1"),
        (axes[1], calibrated, model_calibrated, one - zero, "Calibrated signal"),
    ):
        ax.step(x, ideal, where="mid", color="#d9514e", linewidth=1.6, label="Ideal AllXY")
        ax.plot(x, reference, color="#333e4d", linewidth=1.4, label="Expected probability")
        ax.errorbar(
            x,
            data,
            yerr=error / scale,
            fmt="o",
            markersize=3.8,
            capsize=2,
            color="#187d9c",
            label=f"Simulation: {shots:,} shots per point",
        )
        ax.set(ylabel=ylabel, ylim=(-0.055, 1.08), xlim=(-0.7, 41.7))
        ax.grid(axis="y", alpha=0.2)
    axes[0].legend(loc="upper left", fontsize=9)
    axes[0].set_title("QuMA AllXY sequence — qsbit-sim", loc="left", fontsize=16, weight="bold")
    axes[1].set_xticks(x, [f"{pair}\n{repeat}" for pair in PAIRS for repeat in (1, 2)], fontsize=9)
    axes[1].set_xlabel(
        "Gate pair and consecutive occurrence · X, Y: π rotation · x, y: π/2 rotation"
    )
    params = result["backend_options"]["noise"]["qubits"][0]
    fig.suptitle(
        f"Example parameters: T1 = {params['t1_ns'] / 1000:g} μs, T2 = {params['t2_ns'] / 1000:g} μs"
        " · ideal gates and projective measurement",
        fontsize=11,
        color="#52606b",
    )
    fig.savefig(output / "allxy.png", dpi=180)
    plt.close(fig)
    (output / "allxy.json").write_text(
        json.dumps(
            {
                "source": "https://arxiv.org/abs/1708.07677",
                "shots_per_point": shots,
                "pairs": PAIRS,
                "seed": result["profile"]["seed"],
                "backend_options": result["backend_options"],
                "counts": result["counts"],
                "expected_probabilities": result["expected_probabilities"],
                "timing": result["timing"],
                "program_sha256": result["program_sha256"],
                "versions": result.get("versions", {}),
                "calibration": {"zero_slots": [0, 1], "one_slots": [34, 35, 36, 37]},
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, default=Path("build-clang/quma/results.json"))
    parser.add_argument("--output", type=Path, default=Path("build-clang/quma/figs"))
    args = parser.parse_args()
    result = json.loads(args.results.read_text())
    control = args.results.parent / (args.results.stem + "-control")
    check_control(
        [json.loads(line) for line in (control / "zero.jsonl").read_text().splitlines()],
        json.loads((control / "zero-calls.json").read_text()),
        result["profile"]["start"],
    )
    plot(result, args.output)
    print(f"PASS AllXY numerical and timing checks; figures: {args.output}")
