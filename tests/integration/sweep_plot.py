"""
Write a voltage sweep to disk: the readings as CSV, and their errors as an SVG
plot.

No plotting library. The plot is a few dozen SVG elements, and keeping it here
keeps the test dependencies at pytest, pyserial and pyyaml.
"""

from __future__ import annotations

import math
from pathlib import Path

# One colour per channel, distinguishable for up to fifteen
PALETTE = [
    "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd",
    "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf",
    "#393b79", "#ad494a", "#637939", "#8c6d31", "#7b4173",
]

W, H = 930, 520
LEFT, RIGHT, TOP, BOTTOM = 80, 730, 70, 450  # plot area
X_MAX_MV = 3300

Rows = list[tuple[int, dict[int, int]]]  # (set_mv, {channel: read_mv})


def worst_errors(rows: Rows) -> dict[int, tuple[int, int]]:
    """The largest error on each channel, as {channel: (set_mv, read - set)}."""
    worst: dict[int, tuple[int, int]] = {}
    for set_mv, reads in rows:
        for ch, read_mv in reads.items():
            err = read_mv - set_mv
            if ch not in worst or abs(err) > abs(worst[ch][1]):
                worst[ch] = (set_mv, err)
    return worst


def write_sweep(rows: Rows, stem: Path, tolerance_mv: int, title: str) -> tuple[Path, Path]:
    """
    Write the sweep as stem.csv and stem.svg.

    Returns:
        (csv path, svg path)
    """
    stem.parent.mkdir(parents=True, exist_ok=True)
    channels = sorted(rows[0][1])

    csv_path = stem.with_suffix(".csv")
    with csv_path.open("w") as f:
        f.write("set_mv," + ",".join(f"ch{ch}_mv" for ch in channels) + "\n")
        for set_mv, reads in rows:
            f.write(f"{set_mv}," + ",".join(str(reads[ch]) for ch in channels) + "\n")

    svg_path = stem.with_suffix(".svg")
    svg_path.write_text(render_svg(rows, tolerance_mv, title))
    return csv_path, svg_path


def _nice_step(span: float) -> int:
    """A round grid step that gives four to eight lines across span."""
    for step in (5, 10, 20, 25, 50, 100, 200, 250, 500):
        if span / step <= 8:
            return step
    return 1000


def render_svg(rows: Rows, tolerance_mv: int, title: str) -> str:
    """Error (read - set) against set voltage, one line per channel."""
    channels = sorted(rows[0][1])
    worst = worst_errors(rows)
    biggest = max([abs(e) for _, e in worst.values()] + [tolerance_mv])
    step = _nice_step(2 * biggest * 1.15)
    y_max = step * math.ceil(biggest * 1.15 / step)

    def x(mv: float) -> float:
        return LEFT + (RIGHT - LEFT) * mv / X_MAX_MV

    def y(err: float) -> float:
        return TOP + (BOTTOM - TOP) * (y_max - err) / (2 * y_max)

    font = 'font-family="Helvetica, Arial, sans-serif"'
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
        f'viewBox="0 0 {W} {H}">',
        f'<rect width="{W}" height="{H}" fill="#FFFFFF"/>',
        f'<text x="{LEFT}" y="30" {font} font-size="17" font-weight="700" '
        f'fill="#222">ADC reading minus DAC setting, every channel</text>',
        f'<text x="{LEFT}" y="52" {font} font-size="12" fill="#555">{title} - '
        f'{len(rows)} steps x {len(channels)} channels</text>',
    ]

    # Tolerance band, then the grid over it
    out.append(f'<rect x="{LEFT}" y="{y(tolerance_mv):.1f}" width="{RIGHT - LEFT}" '
               f'height="{y(-tolerance_mv) - y(tolerance_mv):.1f}" fill="#E7F4EA"/>')
    for err in range(-y_max, y_max + 1, step):
        yy = y(err)
        colour = "#888" if err == 0 else "#DDD"
        out.append(f'<line x1="{LEFT}" y1="{yy:.1f}" x2="{RIGHT}" y2="{yy:.1f}" '
                   f'stroke="{colour}" stroke-width="1"/>')
        label = f"{err:+d}" if err else "0"
        out.append(f'<text x="{LEFT - 8}" y="{yy + 4:.1f}" {font} font-size="11" '
                   f'fill="#555" text-anchor="end">{label}</text>')
    for mv in range(0, X_MAX_MV + 1, 500):
        xx = x(mv)
        out.append(f'<line x1="{xx:.1f}" y1="{TOP}" x2="{xx:.1f}" y2="{BOTTOM}" '
                   f'stroke="#EEE" stroke-width="1"/>')
        out.append(f'<text x="{xx:.1f}" y="{BOTTOM + 18}" {font} font-size="11" '
                   f'fill="#555" text-anchor="middle">{mv}</text>')
    for err in (tolerance_mv, -tolerance_mv):
        out.append(f'<line x1="{LEFT}" y1="{y(err):.1f}" x2="{RIGHT}" y2="{y(err):.1f}" '
                   f'stroke="#3C8D50" stroke-width="1.2" stroke-dasharray="6 4"/>')
    out.append(f'<text x="{RIGHT - 6}" y="{y(tolerance_mv) - 6:.1f}" {font} font-size="11" '
               f'fill="#3C8D50" text-anchor="end">tolerance +/-{tolerance_mv} mV</text>')

    # Axis titles
    out.append(f'<text x="{(LEFT + RIGHT) / 2}" y="{BOTTOM + 42}" {font} font-size="12" '
               f'fill="#333" text-anchor="middle">DAC set voltage (mV)</text>')
    out.append(f'<text x="22" y="{(TOP + BOTTOM) / 2}" {font} font-size="12" fill="#333" '
               f'text-anchor="middle" transform="rotate(-90 22 {(TOP + BOTTOM) / 2})">'
               f'reading - setting (mV)</text>')

    # One line per channel; a channel outside the band is drawn heavier
    for i, ch in enumerate(channels):
        colour = PALETTE[i % len(PALETTE)]
        outside = abs(worst[ch][1]) > tolerance_mv
        points = " ".join(f"{x(s):.1f},{y(r[ch] - s):.1f}" for s, r in rows)
        out.append(f'<polyline points="{points}" fill="none" stroke="{colour}" '
                   f'stroke-width="{2.8 if outside else 1.6}" stroke-linejoin="round"/>')

    # Legend: channel and its worst error
    out.append(f'<text x="{RIGHT + 20}" y="{TOP + 4}" {font} font-size="11" '
               f'fill="#555">worst error (mV)</text>')
    for i, ch in enumerate(channels):
        colour = PALETTE[i % len(PALETTE)]
        yy = TOP + 24 + i * 22
        set_mv, err = worst[ch]
        style = 'fill="#C62828" font-weight="700"' if abs(err) > tolerance_mv else 'fill="#333"'
        out.append(f'<line x1="{RIGHT + 20}" y1="{yy - 4}" x2="{RIGHT + 40}" y2="{yy - 4}" '
                   f'stroke="{colour}" stroke-width="3"/>')
        out.append(f'<text x="{RIGHT + 46}" y="{yy}" {font} font-size="12" {style}>'
                   f'ch{ch}  {err:+d} at {set_mv}</text>')

    out.append("</svg>")
    return "\n".join(out) + "\n"
