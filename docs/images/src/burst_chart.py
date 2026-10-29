"""Plot jitter-buffer behaviour on a real phone, before and after the burst fix.

Data: docs/images/data/jitter_{before,after}.csv, per-second stats from `slowik-rx` with a OnePlus streaming over
home Wi-Fi. Output: docs/images/burst.png. Needs matplotlib.
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
BG, PANEL, LINE, TEXT, MUTED = "#0d1117", "#161b22", "#30363d", "#e6edf3", "#8b949e"
RED, GREEN, AMBER = "#e5322d", "#3fb950", "#ffb300"


def load(name):
    with open(os.path.join(DATA, f"jitter_{name}.csv")) as f:
        rows = list(csv.DictReader(f))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


before, after = load("before"), load("after")
n = 25
plt.rcParams.update({"font.family": ["SF Pro Display", "Helvetica Neue", "DejaVu Sans"], "font.size": 13})
fig, ax = plt.subplots(figsize=(16, 5.2), dpi=200)
fig.patch.set_facecolor(BG)
ax.set_facecolor(BG)

ax.plot(before["t_s"][:n], before["latency_ms"][:n], color=RED, lw=3, label="before the fix: anchored on the start-up burst")
ax.plot(after["t_s"][:n], after["latency_ms"][:n], color=GREEN, lw=3, label="after: re-anchor + head-drop")
ax.axhspan(0, 40, color=GREEN, alpha=0.06)
ax.text(24.6, 44, "≤ 40 ms goal", color=GREEN, ha="right", fontsize=12, alpha=0.9)

ax.annotate("Wi-Fi hiccup: 85 ms of excess\naudio dropped, back on target in ~1 s",
            xy=(6, 42), xytext=(8.5, 120), color=TEXT, fontsize=12,
            arrowprops=dict(arrowstyle="->", color=MUTED, lw=1.2))
ax.annotate("~300 ms behind… and never recovers", xy=(14, before["latency_ms"][14]), xytext=(14, 240),
            color=RED, fontsize=12, ha="center", arrowprops=dict(arrowstyle="->", color=RED, lw=1.2))

ax.set_xlim(0, 25)
ax.set_ylim(0, 400)
ax.set_xlabel("seconds since the phone connected", color=MUTED)
ax.set_ylabel("latency to Slowik Mic (ms)", color=MUTED)
ax.tick_params(colors=MUTED)
for s in ax.spines.values():
    s.set_color(LINE)
ax.spines[["top", "right"]].set_visible(False)
ax.grid(axis="y", color=LINE, lw=0.6, alpha=0.6)
leg = ax.legend(loc="center right", bbox_to_anchor=(1, 0.55), frameon=False, labelcolor=TEXT, fontsize=12)
fig.suptitle("Real phone, real Wi-Fi: the first packets arrive as one big clump", x=0.06, ha="left",
             color=TEXT, fontsize=19, fontweight="bold")
fig.tight_layout(rect=(0.02, 0, 1, 0.95))
fig.savefig(os.path.join(HERE, "..", "burst.png"), facecolor=BG)
print("wrote burst.png")
