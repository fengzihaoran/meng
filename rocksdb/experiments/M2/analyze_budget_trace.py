#!/usr/bin/env python3
"""Summarize and render FACO M2 budget trace CSV files.

Typical usage:
  python3 experiments/M2/analyze_budget_trace.py \
    experiments/M2/results/<run>/faco_budget_trace.csv \
    experiments/M2/results/<run>
"""

from __future__ import annotations

import csv
import html
import sys
from pathlib import Path


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def to_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "0") or 0)
    except ValueError:
        return 0.0


def write_svg(rows: list[dict[str, str]], path: Path) -> None:
    width = 900
    height = 320
    left = 56
    right = 24
    top = 24
    bottom = 44
    plot_w = width - left - right
    plot_h = height - top - bottom

    if not rows:
        path.write_text("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n",
                        encoding="utf-8")
        return

    budgets = [to_float(row, "budget") for row in rows]
    pressures = [to_float(row, "p_frag") for row in rows]
    max_budget = max(max(budgets), 1.0)
    max_pressure = max(max(pressures), 1.0)

    def x_at(index: int) -> float:
        if len(rows) == 1:
            return left + plot_w / 2
        return left + plot_w * index / (len(rows) - 1)

    def y_budget(value: float) -> float:
        return top + plot_h * (1.0 - value / max_budget)

    def y_pressure(value: float) -> float:
        return top + plot_h * (1.0 - value / max_pressure)

    budget_points = " ".join(
        f"{x_at(i):.2f},{y_budget(value):.2f}"
        for i, value in enumerate(budgets)
    )
    pressure_points = " ".join(
        f"{x_at(i):.2f},{y_pressure(value):.2f}"
        for i, value in enumerate(pressures)
    )

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="100%" height="100%" fill="#ffffff"/>
  <line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#222" stroke-width="1"/>
  <line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#222" stroke-width="1"/>
  <polyline points="{html.escape(budget_points)}" fill="none" stroke="#0b6e4f" stroke-width="2.5"/>
  <polyline points="{html.escape(pressure_points)}" fill="none" stroke="#b13f2a" stroke-width="2" stroke-dasharray="5 4"/>
  <text x="{left}" y="18" font-family="sans-serif" font-size="13" fill="#0b6e4f">budget</text>
  <text x="{left + 76}" y="18" font-family="sans-serif" font-size="13" fill="#b13f2a">p_frag</text>
  <text x="{left}" y="{height - 12}" font-family="sans-serif" font-size="12" fill="#333">samples: {len(rows)}</text>
</svg>
"""
    path.write_text(svg, encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: analyze_budget_trace.py TRACE_CSV OUT_DIR", file=sys.stderr)
        return 2

    trace_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    rows = read_rows(trace_path) if trace_path.exists() else []
    out_dir.mkdir(parents=True, exist_ok=True)

    budgets = [int(to_float(row, "budget")) for row in rows]
    pressures = [to_float(row, "p_frag") for row in rows]
    dynamic = len(set(budgets)) > 1

    analysis = out_dir / "budget_trace_analysis.md"
    analysis.write_text(
        "\n".join(
            [
                "# M2 budget trace analysis",
                "",
                f"- samples: `{len(rows)}`",
                f"- min budget: `{min(budgets) if budgets else 0}`",
                f"- max budget: `{max(budgets) if budgets else 0}`",
                f"- dynamic budget: `{str(dynamic).lower()}`",
                f"- max p_frag: `{max(pressures) if pressures else 0:.6f}`",
                "",
                "M2 treats RBD/top victim zones as the primary pressure input; "
                "ZVDR contributes only when FACO_BUDGET_ZVDR_WEIGHT is set.",
                "",
            ]
        ),
        encoding="utf-8",
    )
    write_svg(rows, out_dir / "budget_trace.svg")
    print(f"Wrote {analysis}")
    print(f"Wrote {out_dir / 'budget_trace.svg'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
