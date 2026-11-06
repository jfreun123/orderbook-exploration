#!/usr/bin/env python3
"""Draws latency histograms from the CSVs that benchmark/latency writes.

    scripts/plot_latency.py OUT.png TITLE CSV [CSV ...]

Each CSV becomes one series, labelled "<book> (<variant>)". Bars are 1 ns
buckets from 0 to 200 ns, overlaid, with a dashed line at each median, in
the style of the latency chart in David Gross's CppCon 2024 talk.
Anything slower than 200 ns still counts towards the median; the share of
messages past the edge of the chart is printed in the legend.
"""

import csv
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# Categorical slots 1-3, checked for colorblind separation. Text stays in ink
# colours; the series colour only marks the bars and the median lines.
COLORS = ["#2a78d6", "#eb6834", "#1baf7a"]
INK = "#1f1f1e"
MUTED = "#6b6a63"
GRID = "#e4e3de"
SURFACE = "#fcfcfb"
MAX_NS = 200


def load(path):
    counts = defaultdict(int)
    label = None
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            label = f"{row['book']} ({row['variant']})"
            counts[int(row["ns"])] += int(row["count"])
    return label, counts


def median(counts):
    total = sum(counts.values())
    seen = 0
    for ns in sorted(counts):
        seen += counts[ns]
        if seen * 2 >= total:
            return ns
    return 0


def main():
    out, title, *paths = sys.argv[1:]
    if not paths or len(paths) > len(COLORS):
        sys.exit(f"usage: {sys.argv[0]} OUT.png TITLE CSV [CSV ...] (1-3 CSVs)")

    fig, ax = plt.subplots(figsize=(10, 5.6), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    for path, color in zip(paths, COLORS):
        label, counts = load(path)
        total = sum(counts.values())
        xs = list(range(MAX_NS + 1))
        ys = [counts.get(ns, 0) / total * 100 for ns in xs]
        past = sum(c for ns, c in counts.items() if ns > MAX_NS) / total * 100
        med = median(counts)
        ax.bar(xs, ys, width=1.0, color=color, alpha=0.45, linewidth=0,
               label=f"{label}: median {med} ns, {past:.1f}% past {MAX_NS} ns")
        ax.axvline(med, color=color, linestyle="--", linewidth=2)

    ax.set_title(title, color=INK, fontsize=14, loc="left")
    ax.set_xlabel("Latency per message (ns)", color=INK)
    ax.set_ylabel("Share of messages (%)", color=INK)
    ax.set_xlim(0, MAX_NS)
    ax.set_ylim(bottom=0)
    ax.tick_params(colors=MUTED)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(MUTED)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    legend = ax.legend(frameon=False, loc="upper right")
    for text in legend.get_texts():
        text.set_color(INK)

    fig.tight_layout()
    fig.savefig(out, facecolor=SURFACE)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
