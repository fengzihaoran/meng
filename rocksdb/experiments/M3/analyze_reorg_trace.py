#!/usr/bin/env python3
"""Summarize and render FACO M3 reorg planner trace CSV files.

Usage:
  python3 experiments/M3/analyze_reorg_trace.py \
    experiments/M3/results/<run>/faco_reorg_trace.csv \
    experiments/M3/results/<run>
"""

from __future__ import annotations

import csv
import html
import sys
from pathlib import Path


def to_float(value: str | None, default: float = 0.0) -> float:
    """Parse a float from a CSV cell and tolerate missing experiment fields."""
    if value is None or value == "":
        return default
    try:
        return float(value)
    except ValueError:
        return default


def read_rows(trace_csv: Path) -> list[dict[str, str]]:
    """Load trace rows as dictionaries so future columns do not break parsing."""
    with trace_csv.open(newline="") as fh:
        return list(csv.DictReader(fh))


def write_svg(rows: list[dict[str, str]], svg_path: Path) -> None:
    """Write a dependency-free SVG with tau and Net(z) curves."""
    width = 760
    height = 300
    left = 58
    right = 24
    top = 26
    bottom = 42
    plot_w = width - left - right
    plot_h = height - top - bottom

    if not rows:
        svg_path.write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" width="760" height="120">'
            '<text x="24" y="64" font-family="sans-serif" font-size="14">'
            "no reorg samples</text></svg>\n",
            encoding="utf-8",
        )
        return

    nets = [to_float(row.get("net_benefit")) for row in rows]
    taus = [to_float(row.get("tau_trigger")) for row in rows]
    max_y = max(max(nets), max(taus), 1.0)

    def x_at(index: int) -> float:
        if len(rows) == 1:
            return left + plot_w / 2
        return left + plot_w * index / (len(rows) - 1)

    def y_at(value: float) -> float:
        return top + plot_h * (1.0 - value / max_y)

    net_points = " ".join(
        f"{x_at(i):.2f},{y_at(value):.2f}" for i, value in enumerate(nets)
    )
    tau_points = " ".join(
        f"{x_at(i):.2f},{y_at(value):.2f}" for i, value in enumerate(taus)
    )
    accepted_marks = []
    for i, row in enumerate(rows):
        if int(to_float(row.get("accepted"))) == 1:
            accepted_marks.append(
                f'<circle cx="{x_at(i):.2f}" cy="{y_at(nets[i]):.2f}" r="3.5" '
                'fill="#b42318"/>'
            )

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="100%" height="100%" fill="#ffffff"/>
  <line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#8a8f98"/>
  <line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#8a8f98"/>
  <polyline points="{html.escape(net_points)}" fill="none" stroke="#0b5cad" stroke-width="2.4"/>
  <polyline points="{html.escape(tau_points)}" fill="none" stroke="#0b6e4f" stroke-width="2.4" stroke-dasharray="6 4"/>
  {''.join(accepted_marks)}
  <text x="{left}" y="18" font-family="sans-serif" font-size="13" fill="#0b5cad">net benefit</text>
  <text x="{left + 116}" y="18" font-family="sans-serif" font-size="13" fill="#0b6e4f">tau trigger</text>
  <text x="{left}" y="{height - 12}" font-family="sans-serif" font-size="12" fill="#4d5562">samples: {len(rows)}</text>
</svg>
'''
    svg_path.write_text(svg, encoding="utf-8")


def write_analysis(rows: list[dict[str, str]], out_dir: Path) -> None:
    """Write a Markdown summary focused on M3 policy behavior."""
    accepted = [row for row in rows if int(to_float(row.get("accepted"))) == 1]
    budgets = [to_float(row.get("current_budget")) for row in rows]
    nets = [to_float(row.get("net_benefit")) for row in rows]
    taus = [to_float(row.get("tau_trigger")) for row in rows]

    analysis = out_dir / "reorg_trace_analysis.md"
    lines = [
        "# M3 reorg trace analysis",
        "",
        f"- samples: `{len(rows)}`",
        f"- accepted plans: `{len(accepted)}`",
        f"- max net benefit: `{max(nets) if nets else 0:.3f}`",
        f"- last tau trigger: `{taus[-1] if taus else 0:.3f}`",
        f"- min current budget: `{min(budgets) if budgets else 0:.0f}`",
        f"- max current budget: `{max(budgets) if budgets else 0:.0f}`",
        "",
        "M3 uses RBD-ranked top victim zones as the primary candidate source. "
        "ZVDR remains only the optional trend term inside Net(z).",
    ]
    analysis.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: analyze_reorg_trace.py TRACE_CSV OUT_DIR", file=sys.stderr)
        return 2

    trace_csv = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = read_rows(trace_csv) if trace_csv.exists() else []
    write_analysis(rows, out_dir)
    write_svg(rows, out_dir / "reorg_trace.svg")
    print(f"Wrote {out_dir / 'reorg_trace_analysis.md'}")
    print(f"Wrote {out_dir / 'reorg_trace.svg'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
