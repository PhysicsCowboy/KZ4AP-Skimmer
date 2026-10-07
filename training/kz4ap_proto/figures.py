"""The five figures of the development set's analysis (docs/plans/2026-10-06-development-set-redesign.md,
principle 7), drawn from an analysis file (kz4ap_proto.devset2.write_analysis's devset2-<name>.json) as PNG at
150 dpi and SVG, with a caption file listing the runs and the commits each figure was drawn from:

1. CER against S500, one panel per speed cell: the signals as points, the fitted curve at the cell's center, the
   crossing at CER 0.10 with its interval (one figure per decoder).
2. CER against E/N0 per dit, every speed cell's fitted curve on one plot (a time-base-invariant decoder's curves
   coincide), the signals as faint points (one figure per decoder).
3. The crossing (S500 at CER 0.10) against speed, per decoder, the fitted curve with its pointwise interval and the
   cell centers' intervals, beside the genie-aided bound (noncoherent and coherent).
4. Paired differences by group, each variant minus the reference, with 95% intervals over signals.
5. Detection recall through the detector path against S500, per speed cell, beside the decoder's fitted oracle CER
   (one figure per decoder that has detector-path rows).

Needs matplotlib (training/requirements-analysis.txt; not installed by CI).

    python -m kz4ap_proto.experiments figures --analysis build/suite/dev2/experiments/devset2-NAME.json [--dir DIR]
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from kz4ap_synth.jitter import SPEED_CELLS

from .devset2 import Fit, git_commit, ideal_s500_db

DPI = 150
S500_LABEL = "S$_{500}$ (dB SNR in 500 Hz)"
EN0_LABEL = "E/N$_0$ per dit (dB re 1; key-down energy in one dit, 1.2 s / WPM)"
SPEED_LABEL = "speed (WPM)"
CER_LABEL = "CER (edits per reference symbol)"
# Categorical slots in fixed order (identity: decoders, variants); the reference palette of the dataviz method.
SERIES = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948")
# One-hue ordinal ramp, light to dark, for the 10 speed cells (magnitude: slow light, fast dark).
SPEED_RAMP = ("#86b6ef", "#6da7ec", "#5598e7", "#3987e5", "#2a78d6", "#256abf", "#1c5cab", "#184f95", "#104281",
              "#0d366b")
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e4e3df"


def _plt():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK, "xtick.color": MUTED,
                         "ytick.color": MUTED, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6,
                         "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False,
                         "svg.hashsalt": "kz4ap", "svg.fonttype": "none"})
    return plt


def _save(fig, stem: Path) -> list[Path]:
    stem.parent.mkdir(parents=True, exist_ok=True)
    paths = [stem.with_suffix(".png"), stem.with_suffix(".svg")]
    fig.savefig(paths[0], dpi=DPI, bbox_inches="tight")
    fig.savefig(paths[1], bbox_inches="tight", metadata={"Date": None})
    import matplotlib.pyplot as plt
    plt.close(fig)
    return paths


def _fit_rows(a: dict, decoder: str) -> list[dict]:
    groups = set(a["meta"]["fit_groups"])
    return [r for r in a["signals"] if r["decoder"] == decoder and r["path"] == "oracle" and r["group"] in groups]


def _safe(name: str) -> str:
    return "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in name)


OUTSIDE_PAD_DB = 1.0  # the S500 axes span the fitted data's range plus this margin, dB SNR in 500 Hz (presentation)


def s500_axis_limits(ranges, extra=()) -> tuple[float, float]:
    """The S500 axis span (dB SNR in 500 Hz) of figures 1 and 3: the fitted data's ranges (pairs lo, hi), widened
    to include the finite `extra` values (e.g. the genie bound), plus OUTSIDE_PAD_DB on each side. A crossing or
    interval outside the data (an extrapolation, which can reach hundreds of dB) does not set the scale; the
    figures mark it at the edge with its value instead."""
    values = [v for r in ranges for v in r] + [float(v) for v in extra if v is not None and np.isfinite(v)]
    return min(values) - OUTSIDE_PAD_DB, max(values) + OUTSIDE_PAD_DB


def figure_cer_s500(a: dict, decoder: str, stem: Path) -> list[Path] | None:
    """Figure 1 for one decoder. The S500 axis spans the data (s500_axis_limits); a crossing beyond it is marked by
    an arrow at the edge with its value."""
    fit = a["decoders"][decoder]["fit"]
    if fit is None:
        return None
    plt = _plt()
    model = Fit.from_json(fit["s500"]["fit"])
    rows = _fit_rows(a, decoder)
    x_lo, x_hi = s500_axis_limits([fit["s500_range_db"]])
    grid = np.linspace(x_lo, x_hi, 200)
    fig, axes = plt.subplots(2, 5, figsize=(13, 5.6), sharex=True, sharey=True)
    for ax, cell in zip(axes.flat, fit["s500"]["crossings_0.10"]):
        k = cell["cell"]
        mine = [r for r in rows if r["speed_cell"] == k]
        ax.scatter([r["s500_db"] for r in mine], [min(r["edits"] / r["symbols"], 1.0) for r in mine], s=9,
                   color=SERIES[0], alpha=0.55, linewidths=0, label="signals (CER clipped at 1)")
        ax.plot(grid, model.cer(grid, np.full_like(grid, cell["wpm"])), color=INK, lw=1.5,
                label="fit at the cell's center")
        ax.axhline(0.10, color=MUTED, lw=0.8, ls=":")
        if cell["value"] is not None:
            if cell["interval"]:
                ax.axvspan(*cell["interval"], color=SERIES[1], alpha=0.2, lw=0, label="crossing's 95% interval")
            ax.axvline(cell["value"], color=SERIES[1], lw=1.2, label="S$_{500}$ at CER 0.10")
            if not x_lo <= cell["value"] <= x_hi:
                right = cell["value"] > x_hi
                ax.annotate(f"crossing {cell['value']:+.1f} dB\n(outside the data)", xy=(x_hi if right else x_lo, 0.5),
                            xytext=(-4 if right else 4, 0), textcoords="offset points", ha="right" if right else "left",
                            va="center", fontsize=7, color=SERIES[1])
                ax.plot([x_hi if right else x_lo], [0.5], ">" if right else "<", color=SERIES[1], ms=6, clip_on=False)
        elif cell.get("no_crossing"):
            # top right: the curve sits at the top only at low S500 (left) and at its floor at high S500
            ax.text(0.97, 0.95, f"no crossing at CER 0.10\n(floor {cell['floor']:.3f})", transform=ax.transAxes,
                    ha="right", va="top", fontsize=7, color=SERIES[1])
        ax.set_xlim(x_lo, x_hi)
        r_lo, r_hi = SPEED_CELLS.bounds(k)
        ax.set_title(f"cell {k}: {r_lo:.1f}–{r_hi:.1f} WPM ({len(mine)} signals)", fontsize=8, color=INK)
        ax.set_ylim(-0.03, 1.03)
    for ax in axes[1]:
        ax.set_xlabel(S500_LABEL)
    for ax in axes[:, 0]:
        ax.set_ylabel(CER_LABEL)
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=4, bbox_to_anchor=(0.5, 1.03))
    fig.suptitle(f"{decoder}: CER against S$_{{500}}$ per speed cell (dotted: CER 0.10)", y=1.07, color=INK)
    return _save(fig, stem)


def figure_cer_en0(a: dict, decoder: str, stem: Path) -> list[Path] | None:
    """Figure 2 for one decoder."""
    fit = a["decoders"][decoder]["fit"]
    if fit is None:
        return None
    plt = _plt()
    model = Fit.from_json(fit["en0"]["fit"])
    rows = _fit_rows(a, decoder)
    levels = [r["en0_db"] for r in rows]
    grid = np.linspace(min(levels) - 1.0, max(levels) + 1.0, 300)
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for cell in fit["en0"]["crossings_0.10"]:
        k = cell["cell"]
        color = SPEED_RAMP[k - 1]
        mine = [r for r in rows if r["speed_cell"] == k]
        ax.scatter([r["en0_db"] for r in mine], [min(r["edits"] / r["symbols"], 1.0) for r in mine], s=5,
                   color=color, alpha=0.25, linewidths=0)
        ax.plot(grid, model.cer(grid, np.full_like(grid, cell["wpm"])), color=color, lw=1.5,
                label=f"cell {k}, {cell['wpm']:.1f} WPM")
    ideal = fit["en0"]["ideal_noncoherent_db"]
    ax.axvline(ideal, color=MUTED, lw=1.0, ls="--", label=f"genie bound at CER 0.10, noncoherent ({ideal:.1f} dB re 1)")
    ax.axhline(0.10, color=MUTED, lw=0.8, ls=":")
    ax.set_xlabel(EN0_LABEL)
    ax.set_ylabel(CER_LABEL)
    ax.set_ylim(-0.03, 1.03)
    spread = fit["en0"]["spread_db"]
    ax.set_title(f"{decoder}: CER against E/N$_0$ per dit; a time-base-invariant decoder's curves coincide\n"
                 f"spread of the crossings at CER 0.10: {spread:.2f} dB of E/N$_0$ per dit" if spread is not None else decoder,
                 fontsize=9, color=INK)
    ax.legend(loc="upper right", fontsize=7)
    return _save(fig, stem)


def figure_crossings(a: dict, stem: Path, threshold: float = 0.10) -> list[Path] | None:
    """Figure 3: every decoder's crossing against speed, beside the genie-aided bound. The S500 axis spans the fitted
    data and the bound (s500_axis_limits); a cell's crossing beyond it is marked by a triangle at the edge with its
    value, and bands beyond it are cut at the edge."""
    fitted = [(name, e["fit"]) for name, e in a["decoders"].items() if e["fit"] is not None]
    if not fitted:
        return None
    plt = _plt()
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    key = f"{threshold:.2f}"
    v_all = np.geomspace(SPEED_CELLS.edges[0], SPEED_CELLS.edges[-1], 100)
    y_lo, y_hi = s500_axis_limits([fit["s500_range_db"] for _, fit in fitted],
                                  extra=[*ideal_s500_db(threshold, v_all, coherent=True), *ideal_s500_db(threshold, v_all)])
    for i, (name, fit) in enumerate(fitted):
        color = SERIES[i % len(SERIES)]
        curve = fit["s500"][f"curve_{key}"]
        v = np.array([c["wpm"] for c in curve])
        y = np.array([np.nan if c["value"] is None else c["value"] for c in curve])
        lo = np.array([np.nan if not c["interval"] else c["interval"][0] for c in curve])
        hi = np.array([np.nan if not c["interval"] else c["interval"][1] for c in curve])
        ax.fill_between(v, lo, hi, color=color, alpha=0.15, lw=0)
        ax.plot(v, y, color=color, lw=1.8, label=f"{name} (fit, pointwise 95% interval)")
        cells = fit["s500"][f"crossings_{key}"]
        # a percentile interval need not contain the point estimate: drawn as its own segment, not as error bars
        shown = [c for c in cells if c["value"] is not None]
        ax.plot([c["wpm"] for c in shown], [c["value"] for c in shown], "o", ms=4, color=color)
        spans = [c for c in shown if c["interval"]]
        ax.vlines([c["wpm"] for c in spans], [c["interval"][0] for c in spans], [c["interval"][1] for c in spans],
                  color=color, lw=1.2)
        for c in shown:
            if not y_lo <= c["value"] <= y_hi:
                top = c["value"] > y_hi
                edge = y_hi if top else y_lo
                ax.plot([c["wpm"]], [edge], "^" if top else "v", ms=7, color=color, clip_on=False)
                ax.annotate(f"{c['value']:+.1f} dB", xy=(c["wpm"], edge), xytext=(6, -10 if top else 4),
                            textcoords="offset points", fontsize=7, color=color)
        missing = [c for c in cells if c["value"] is None and c.get("no_crossing")]
        for c in missing:
            ax.plot([c["wpm"]], [y_hi], "x", ms=7, mew=1.5, color=color, clip_on=False)
        if missing:
            # one line per decoder, stacked under the top edge at the axis's middle (cells' x markers sit at the edge)
            ax.text(0.5, 0.97 - 0.045 * i, f"{name}: no crossing in speed cell{'s' if len(missing) > 1 else ''} "
                    + ", ".join(str(c["cell"]) for c in missing if "cell" in c) + " (x at the top)",
                    transform=ax.transAxes, ha="center", va="top", fontsize=7, color=color)
    v = v_all
    ax.plot(v, ideal_s500_db(threshold, v), color=MUTED, lw=1.2, ls="--", label="genie bound, noncoherent")
    ax.plot(v, ideal_s500_db(threshold, v, coherent=True), color=MUTED, lw=1.0, ls=":", label="genie bound, coherent")
    ax.set_ylim(y_lo, y_hi)
    ax.set_xscale("log")
    ticks = [8, 10, 12, 15, 20, 25, 30, 40, 50, 60, 80]
    ax.set_xticks(ticks)
    ax.set_xticklabels([str(t) for t in ticks])
    ax.minorticks_off()
    ax.set_xlabel(SPEED_LABEL)
    ax.set_ylabel(f"S$_{{500}}$ at CER {threshold:.2f} (dB SNR in 500 Hz)")
    ax.set_title(f"S$_{{500}}$ needed for CER {threshold:.2f} against speed (lower is better); points: cell centers",
                 fontsize=9, color=INK)
    ax.legend(fontsize=7)
    return _save(fig, stem)


def figure_paired(a: dict, stem: Path) -> list[Path] | None:
    """Figure 4: paired differences by group, each variant minus the reference."""
    comps = [c for c in a["paired"] if c["all"] is not None]
    if not comps:
        return None
    plt = _plt()
    groups = sorted({g for c in comps for g in c["groups"]})
    labels = ["all"] + groups
    fig, ax = plt.subplots(figsize=(7.5, 0.9 + 0.32 * len(labels) * max(1, len(comps))))
    step = 0.8 / len(comps)
    for i, comp in enumerate(comps):
        color = SERIES[(i + 1) % len(SERIES)]
        for j, label in enumerate(labels):
            s = comp["all"] if label == "all" else comp["groups"].get(label)
            if s is None:
                continue
            y = j + (i - (len(comps) - 1) / 2) * step
            iv = s["interval"]
            if iv:
                ax.plot(iv, [y, y], color=color, lw=1.5)
            ax.plot([s["mean"]], [y], "o", ms=5, color=color,
                    label=f"{comp['variant']} − {comp['reference']}" if j == 0 else None)
    ax.axvline(0.0, color=MUTED, lw=0.8)
    ax.set_yticks(range(len(labels)))
    ax.set_yticklabels([f"{lab}" for lab in labels])
    ax.invert_yaxis()
    ax.set_xlabel("paired CER difference, variant − reference (per signal; negative: the variant is better)")
    ax.set_title("Paired differences by group, mean and 95% interval over signals", fontsize=9, color=INK)
    ax.legend(fontsize=7, loc="lower right")
    return _save(fig, stem)


def figure_recall(a: dict, decoder: str, stem: Path) -> list[Path] | None:
    """Figure 5 for one decoder: recall against S500 per speed cell, beside its fitted oracle CER."""
    entry = a["decoders"][decoder]
    if not entry["recall"]:
        return None
    plt = _plt()
    model = Fit.from_json(entry["fit"]["s500"]["fit"]) if entry["fit"] else None
    fig, axes = plt.subplots(2, 5, figsize=(13, 5.6), sharex=True, sharey=True)
    s_lo = min(c["mean_s500_db"] for c in entry["recall"]) - 1.0
    s_hi = max(c["mean_s500_db"] for c in entry["recall"]) + 1.0
    grid = np.linspace(s_lo, s_hi, 200)
    for ax, k in zip(axes.flat, SPEED_CELLS.numbers):
        cells = [c for c in entry["recall"] if c["speed_cell"] == k]
        if cells:
            x = [c["mean_s500_db"] for c in cells]
            y = [c["recall"] for c in cells]
            # the Wilson interval holds the recall; max(0, ...) only absorbs rounding at 0 and 1
            err = np.array([[max(0.0, c["recall"] - c["interval"][0]), max(0.0, c["interval"][1] - c["recall"])]
                            for c in cells]).T
            ax.errorbar(x, y, yerr=err, fmt="o-", ms=3.5, lw=1.2, capsize=2, color=SERIES[0],
                        label="detection recall (Wilson 95%)")
        binned = [b for b in entry["binned_cer"] if b["speed_cell"] == k]
        if binned:
            ax.plot([b["mean_s500_db"] for b in binned], [min(b["cer"], 1.0) for b in binned], "s", ms=3,
                    color=SERIES[1], label="oracle CER, pooled per S$_{500}$ cell")
        if model is not None:
            center = SPEED_CELLS.center(k)
            ax.plot(grid, model.cer(grid, np.full_like(grid, center)), color=SERIES[1], lw=1.2,
                    label="oracle CER, fit at the cell's center")
        r_lo, r_hi = SPEED_CELLS.bounds(k)
        ax.set_title(f"cell {k}: {r_lo:.1f}–{r_hi:.1f} WPM", fontsize=8, color=INK)
        ax.set_ylim(-0.03, 1.03)
    for ax in axes[1]:
        ax.set_xlabel(S500_LABEL)
    for ax in axes[:, 0]:
        ax.set_ylabel("recall; CER")
    handles, labels = [], []
    for ax in axes.flat:
        for h, lab in zip(*ax.get_legend_handles_labels()):
            if lab not in labels:
                handles.append(h)
                labels.append(lab)
    fig.legend(handles, labels, loc="upper center", ncol=3, bbox_to_anchor=(0.5, 1.03))
    fig.suptitle(f"{decoder}: detection recall through the detector path, beside the oracle CER", y=1.07, color=INK)
    return _save(fig, stem)


CAPTIONS = {
    "fig1": "CER against S500 (dB SNR in 500 Hz), one panel per speed cell: the signals (points), the fitted curve at "
            "the cell's center (the smooth fit over S500 and ln WPM, one floor per speed cell), the crossing at CER "
            "0.10 with its 95% bootstrap interval over signals, or \"no crossing\" with the cell's floor.",
    "fig2": "CER against E/N0 per dit (dB re 1), every speed cell's fitted curve at its center: a time-base-invariant "
            "decoder's curves coincide; the dashed line is the genie-aided bound at CER 0.10 (noncoherent).",
    "fig3": "S500 at CER 0.10 (dB SNR in 500 Hz) against speed (WPM), per decoder: the fit with its pointwise 95% "
            "interval and the cell centers' intervals (x at the top: no crossing in that cell), beside the genie-aided "
            "bound (noncoherent dashed, coherent dotted; derived, stage-1 results record section 5.1.1, 10 units per "
            "character heuristic). The fitted curve steps at cell edges where the floors of neighboring cells differ.",
    "fig4": "Paired CER differences, variant minus reference, per signal, by group and pooled: mean and 95% bootstrap "
            "interval over signals.",
    "fig5": "Detection recall through the detector path (A2's detector copies) against S500 (dB SNR in 500 Hz), per "
            "speed cell, with Wilson 95% intervals, beside the same decoder's oracle CER (pooled per S500 cell and "
            "fitted).",
}


def draw_all(analysis_path, out_dir=None) -> list[Path]:
    """Draws every figure the analysis supports into out_dir (default: figures-<name> beside the analysis file) and
    writes captions.md there. Returns the files written."""
    analysis_path = Path(analysis_path)
    a = json.loads(analysis_path.read_text(encoding="utf-8"))
    name = a["meta"].get("name") or analysis_path.stem
    out_dir = Path(out_dir) if out_dir else analysis_path.parent / f"figures-{_safe(name)}"
    drawn: list[tuple[str, str, list[Path]]] = []
    for decoder in a["meta"]["decoders"]:
        for tag, fn in (("fig1", figure_cer_s500), ("fig2", figure_cer_en0), ("fig5", figure_recall)):
            paths = fn(a, decoder, out_dir / f"{tag}-{_safe(decoder)}")
            if paths:
                drawn.append((tag, decoder, paths))
    for tag, fn in (("fig3", figure_crossings), ("fig4", figure_paired)):
        paths = fn(a, out_dir / tag)
        if paths:
            drawn.append((tag, "all decoders" if tag == "fig3" else "paired comparisons", paths))
    m = a["meta"]
    now = git_commit()
    runs = ", ".join(m["decoders"])
    lines = [f"# Figures: {name}", "",
             f"Drawn from `{analysis_path.name}` (analysis code at {m.get('commit')}"
             f"{' with uncommitted changes' if m.get('modified') else ''}; figures code at {now['commit']}"
             f"{' with uncommitted changes' if now['modified'] else ''}).",
             f"Runs (decoders by results folder): {runs}; reference {m.get('reference')}; results roots "
             f"{', '.join(m.get('results_dirs', []))} (relative to the suite folder); test cases `{m.get('only')}`; fit groups "
             f"{', '.join(m['fit_groups'])} (oracle); fit method {m['method']}; {m['resamples']} bootstrap resamples.",
             ""]
    for tag, what, paths in sorted(drawn, key=lambda d: (d[0], d[1])):
        lines.append(f"- **{paths[0].stem}** ({what}): {CAPTIONS[tag]} Files: {', '.join(p.name for p in paths)}.")
    out_dir.mkdir(parents=True, exist_ok=True)
    captions = out_dir / "captions.md"
    captions.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return [p for _, _, paths in drawn for p in paths] + [captions]
