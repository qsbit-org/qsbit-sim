"""Plot the Figure 3 gate sequence and scalar versus dual-codeword issue results."""

import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import ListedColormap

MODES = ("rv32", "vliw-scalar", "vliw-bundle")
LABELS = ("RV32 · scalar", "VLIW · scalar", "VLIW · dual cw")
COLORS = ("#516579", "#cb7b23", "#007f87")


def plot_sequence(report, output):
    first = report["figure3"]["rv32"]["operation_starts"]
    second = report["figure3"]["vliw"]["operation_starts"]
    if first != second or not first:
        raise ValueError("CPU models must produce identical nonempty gate sequences")
    origin = min(event[0] for event in first)
    targets = sorted({target for _, qubits, _ in first for target in qubits})
    names = {"y": "Y", "rx": "X90", "x": "X", "measure": "Measure"}
    fig, ax = plt.subplots(figsize=(9, 3.6), layout="constrained")
    for row, target in enumerate(targets):
        events = [
            (tick - origin, names.get(operation, operation))
            for tick, qubits, operation in first
            if target in qubits
        ]
        times = [tick for tick, _ in events]
        ax.hlines(row, min(times), max(times), color="#c6d1d9", linewidth=2)
        ax.scatter(times, [row] * len(times), color=COLORS[2], s=90, zorder=3)
        for tick, name in events:
            ax.annotate(
                name,
                (tick, row),
                xytext=(0, 13),
                textcoords="offset points",
                ha="center",
                weight="bold",
                fontsize=12,
            )
    times = sorted({event[0] - origin for event in first})
    span = max(times) or 1
    ax.set(
        xticks=times,
        yticks=range(len(targets)),
        yticklabels=[f"q{q}" for q in targets],
        xlabel=f"Trigger time relative to first Y gate (ns) · first Y at {origin:,} ns",
        xlim=(-0.18 * span, 1.18 * span),
        ylim=(-0.55, len(targets) - 0.35),
    )
    ax.invert_yaxis()
    ax.set_title("eQASM Figure 3 gate sequence", loc="left", weight="bold", pad=28)
    ax.text(
        0,
        1.04,
        "qsbit-sim · identical RV32 and VLIW trigger times",
        transform=ax.transAxes,
        fontsize=10,
        color="#516579",
    )
    ax.grid(axis="x", color="#dce3e8", linewidth=0.8)
    ax.spines["left"].set_visible(False)
    ax.tick_params(axis="y", length=0)
    fig.savefig(output / "figure3.png", dpi=180)
    plt.close(fig)


def plot_issue(report, output):
    cases = report["issue_rate"]
    intervals = sorted({case["interval_ns"] for case in cases})
    indexed = {(case["mode"], case["interval_ns"]): case for case in cases}
    if len(indexed) != len(cases) or set(indexed) != {
        (mode, interval) for mode in MODES for interval in intervals
    }:
        raise ValueError("issue-rate results must contain one case per mode and interval")
    if any(not case["success"] and case["fault"] != "LateAdmission" for case in cases):
        raise ValueError("issue-rate runs failed for a reason other than LateAdmission")
    profile = report["settings"]["profile"]
    cpu = profile["cpu"]["period"]
    matrix = np.array(
        [[indexed[mode, interval]["success"] for interval in intervals] for mode in MODES]
    )
    fig, (rate, completion) = plt.subplots(
        1, 2, figsize=(13, 4.8), gridspec_kw={"width_ratios": [1.1, 1]}, layout="constrained"
    )
    for mode, label, color, marker in zip(MODES, LABELS, COLORS, ("o", "x", "s")):
        values = [
            indexed[mode, interval]["first_eight_mean_submission_interval_ns"] / cpu
            for interval in intervals
        ]
        rate.plot(
            intervals,
            values,
            label=label,
            color=color,
            marker=marker,
            markersize=8,
            linewidth=1.7,
            linestyle="--" if mode == "vliw-scalar" else "-",
        )
    rate.set(
        xlabel="Scheduled gate-pair interval (ns)",
        ylabel="Mean submission interval (CPU cycles)",
        xticks=intervals,
        ylim=(0, rate.get_ylim()[1] * 1.15),
    )
    rate.set_title("First eight time-point submissions", loc="left", fontsize=12)
    rate.grid(axis="y", alpha=0.2)
    rate.legend(loc="lower right", fontsize=10)
    completion.imshow(
        matrix, cmap=ListedColormap(["#f5ddd9", "#cce9e3"]), vmin=0, vmax=1, aspect="auto"
    )
    for row in range(len(MODES)):
        for column in range(len(intervals)):
            completion.text(
                column,
                row,
                "OK" if matrix[row, column] else "Late",
                ha="center",
                va="center",
                fontsize=11,
                color="#233746",
            )
    completion.set(
        xticks=range(len(intervals)),
        xticklabels=intervals,
        yticks=range(len(MODES)),
        yticklabels=LABELS,
        xlabel="Scheduled gate-pair interval (ns)",
    )
    completion.set_title(
        f"Completion of all {report['points']} gate pairs", loc="left", fontsize=12
    )
    completion.tick_params(length=0, pad=8)
    for spine in completion.spines.values():
        spine.set_visible(False)
    fig.suptitle(
        f"qsbit-sim · CPU {cpu} ns · TCU {profile['tcu']['period']} ns"
        f" · {report['settings']['backend']} backend",
        fontsize=14,
        weight="bold",
    )
    fig.savefig(output / "issue_rate.png", dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path("build-clang/eqasm/results.json"))
    parser.add_argument("--output", type=Path, default=Path("examples/eqasm/figs"))
    args = parser.parse_args()
    report = json.loads(args.results.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update(
        {
            "font.size": 11,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "font.family": "DejaVu Sans",
        }
    )
    plot_sequence(report, args.output)
    plot_issue(report, args.output)
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Figures and result data: {args.output}")


if __name__ == "__main__":
    main()
