"""Draws tools/jet_obstacle_sim.cpp runs: top (x-z) and side (distance along the first run's start heading vs height)
views of one or more runs of the same scenario over its boxes (read from the run's log, "SIM box" lines).

    python tools/jet_obstacle_view.py OUT.png RUN.csv [RUN.csv ...]

Each RUN.csv needs its RUN.log beside it. The legend names a run by its file name; dots mark where a box held the body.
"""
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.patches as patches  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402


def load(path: str) -> tuple[list[dict[str, float]], list[list[float]], list[float] | None]:
    with open(path, newline="") as f:
        rows = [{k: (float(v) if k != "mode" else 0.0) for k, v in r.items()} for r in csv.DictReader(f)]
    boxes: list[list[float]] = []
    target = None
    with open(path[:-4] + ".log") as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 9 and parts[1] == "SIM" and parts[2] == "box":
                boxes.append([float(x) for x in parts[3:9]])
            elif len(parts) >= 6 and parts[1] == "SIM" and parts[2] == "target":
                target = [float(x) for x in parts[3:6]]
    return rows, boxes, target


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 1
    out, runs = argv[1], argv[2:]
    fig, (top, side) = plt.subplots(1, 2, figsize=(15, 6.5))
    colors = ["#2a6fdb", "#d1495b", "#2e933c", "#8f5bd6", "#e08e0b"]
    boxes: list[list[float]] = []
    target = None
    for i, path in enumerate(runs):
        rows, boxes, target = load(path)
        c = colors[i % len(colors)]
        name = os.path.basename(path)[:-4]
        top.plot([r["x"] for r in rows], [r["z"] for r in rows], color=c, lw=1.4, label=name)
        side.plot([r["z"] for r in rows], [r["y"] for r in rows], color=c, lw=1.4, label=name)
        held = [r for r in rows if r["held"] > 0]
        top.scatter([r["x"] for r in held], [r["z"] for r in held], color=c, s=10)
        side.scatter([r["z"] for r in held], [r["y"] for r in held], color=c, s=10)
    for b in boxes:
        top.add_patch(patches.Rectangle((b[0], b[2]), b[3] - b[0], b[5] - b[2], color="#777", alpha=0.45))
        side.add_patch(patches.Rectangle((b[2], b[1]), b[5] - b[2], b[4] - b[1], color="#777", alpha=0.25))
    if target:
        top.plot(target[0], target[2], "kx", ms=10)
        side.plot(target[2], target[1], "kx", ms=10)
    top.set_aspect("equal", adjustable="datalim")
    top.set_xlabel("x (m)")
    top.set_ylabel("z (m)")
    top.set_title("top")
    side.set_xlabel("z (m)")
    side.set_ylabel("height (m)")
    side.set_title("side (all boxes projected on z)")
    side.axhline(0, color="#444", lw=0.8)
    for ax in (top, side):
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=110)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
