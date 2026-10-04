"""Plot synchronized output signals and BISP booking overhead."""

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--results", type=Path, default=Path("build-clang/distributed-hisq/results.json")
)
parser.add_argument("--output", type=Path, default=Path(__file__).with_name("figs"))
args = parser.parse_args()
report = json.loads(args.results.read_text())
trace = [
    json.loads(line)
    for line in (args.results.parent / "boards/trace.jsonl").read_text().splitlines()
]
args.output.mkdir(parents=True, exist_ok=True)
plt.rcParams.update({"font.size": 11, "axes.spines.top": False, "axes.spines.right": False})
origin = report["settings"]["profile"]["start"]
fig, ax = plt.subplots(figsize=(10, 4.3), layout="constrained")
lanes = [
    (1, 21, "Control: outer loop", "#7d8798"),
    (1, 20, "Control: inner loop", "#32a6a0"),
    (1, 7, "Control: synchronized output", "#d59916"),
    (2, report["settings"]["profile"]["ports"] + 5, "Readout: synchronized output", "#397bc2"),
]
for row, (core, port, label, color) in enumerate(lanes):
    starts = [
        e
        for e in trace
        if e["kind"] == "OperationStart" and e.get("core") == core and e["port"] == port
    ]
    for event in starts:
        ax.vlines((event["tick"] - origin) / 1000, row - 0.25, row + 0.25, color=color, linewidth=2)
    ax.hlines(
        row - 0.25, 0, (report["boards"]["stop_tick"] - origin) / 1000, color=color, alpha=0.3
    )
ax.set_yticks(range(len(lanes)), [lane[2] for lane in lanes])
ax.invert_yaxis()
ax.set_xlabel("Time after TCU start (µs)")
skews = report["boards"]["output_skew_ns"]
ax.set_title(
    f"Distributed-HISQ neighbor synchronization\n{len(skews)} output pairs · {max(skews)} ns inter-core skew"
)
ax.grid(axis="x", alpha=0.15)
fig.savefig(args.output / "neighbor-synchronization.png", dpi=180)
plt.close(fig)

fig, ax = plt.subplots(figsize=(7, 4.4), layout="constrained")
scan = report["booking_scan"]
for latency in sorted({row["latency_cycles"] for row in scan}):
    rows = [row for row in scan if row["latency_cycles"] == latency]
    ax.plot(
        [row["duration_cycles"] for row in rows],
        [row["overhead_cycles"] for row in rows],
        "o-",
        markersize=4,
        label=f"Link delay: {latency} cycles",
    )
ax.set_xlabel("Deterministic work before the synchronized operation (cycles)")
ax.set_ylabel("Synchronization overhead (cycles)")
ax.set_title("BISP hides communication behind deterministic work")
ax.grid(alpha=0.2)
ax.legend(frameon=False)
fig.savefig(args.output / "booking-overhead.png", dpi=180)
