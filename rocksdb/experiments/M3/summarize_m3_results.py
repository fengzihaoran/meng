#!/usr/bin/env python3
"""Aggregate FACO M3 compare results.

The script reads the result tree produced by run_reorg_compare.sh and writes a
small Markdown report plus CSV sidecars for runtime and reorg trace metrics.
"""

from __future__ import annotations

import csv
import sys
from collections import defaultdict
from pathlib import Path


def to_float(value: str | None, default: float = 0.0) -> float:
    """Parse numeric cells from logs without failing on ERROR rows."""
    if value is None or value == "":
        return default
    try:
        return float(value)
    except ValueError:
        return default


def parse_kv(path: Path) -> dict[str, str]:
    """Parse key=value files emitted by ZenFS FACO runtime metrics."""
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def scenario_from_run_dir(run_dir: Path) -> tuple[str, str]:
    """Return variant and run id from .../<variant>/run_N directories."""
    return run_dir.parent.name, run_dir.name


def collect_runtime_rows(root: Path) -> list[dict[str, str]]:
    """Collect reset, finish, and write amplification counters."""
    rows: list[dict[str, str]] = []
    for metrics_path in sorted(root.rglob("faco_runtime_metrics.txt")):
        run_dir = metrics_path.parent
        variant, run_name = scenario_from_run_dir(run_dir)
        metrics = parse_kv(metrics_path)
        user = to_float(metrics.get("user_bytes_written"))
        total = to_float(metrics.get("bytes_written"))
        wa = total / user if user > 0 else 0.0
        rows.append(
            {
                "variant": variant,
                "run": run_name,
                "zone_reset_count": metrics.get("zone_reset_count", ""),
                "zone_finish_count": metrics.get("zone_finish_count", ""),
                "bytes_written": metrics.get("bytes_written", ""),
                "gc_bytes_written": metrics.get("gc_bytes_written", ""),
                "user_bytes_written": metrics.get("user_bytes_written", ""),
                "write_amplification": f"{wa:.6f}",
                "result_dir": str(run_dir),
            }
        )
    return rows


def collect_reorg_rows(root: Path) -> list[dict[str, str]]:
    """Collect one summary row per M3 reorg trace."""
    rows: list[dict[str, str]] = []
    for trace_path in sorted(root.rglob("faco_reorg_trace.csv")):
        run_dir = trace_path.parent
        variant, run_name = scenario_from_run_dir(run_dir)
        with trace_path.open(newline="") as fh:
            samples = list(csv.DictReader(fh))
        accepted = [row for row in samples if int(to_float(row.get("accepted"))) == 1]
        rows.append(
            {
                "variant": variant,
                "run": run_name,
                "samples": str(len(samples)),
                "accepted_plans": str(len(accepted)),
                "max_net_benefit": f"{max([to_float(r.get('net_benefit')) for r in samples], default=0.0):.3f}",
                "last_tau_trigger": f"{to_float(samples[-1].get('tau_trigger')) if samples else 0.0:.3f}",
                "result_dir": str(run_dir),
            }
        )
    return rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    """Write dict rows while preserving field order from the first row."""
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def mean(values: list[float]) -> float:
    """Return zero for empty groups to keep reports compact."""
    return sum(values) / len(values) if values else 0.0


def group_by_variant(rows: list[dict[str, str]]) -> dict[str, list[dict[str, str]]]:
    """Group metric rows by variant name."""
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row.get("variant", "")].append(row)
    return grouped


def write_markdown(root: Path, out_md: Path, runtime_rows: list[dict[str, str]], reorg_rows: list[dict[str, str]]) -> None:
    """Write the M3 rollup report."""
    runtime_by_variant = group_by_variant(runtime_rows)
    reorg_by_variant = group_by_variant(reorg_rows)
    lines = [
        "# M3 reorg summary",
        "",
        f"- result root: `{root}`",
        f"- runtime rows: `{len(runtime_rows)}`",
        f"- reorg trace rows: `{len(reorg_rows)}`",
        "",
        "## Runtime Metrics",
        "",
        "| variant | runs | avg resets | avg finishes | avg WA | avg GC GB |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for variant, group in sorted(runtime_by_variant.items()):
        lines.append(
            "| {} | {} | {:.2f} | {:.2f} | {:.4f} | {:.3f} |".format(
                variant,
                len(group),
                mean([to_float(r.get("zone_reset_count")) for r in group]),
                mean([to_float(r.get("zone_finish_count")) for r in group]),
                mean([to_float(r.get("write_amplification")) for r in group]),
                mean([to_float(r.get("gc_bytes_written")) for r in group])
                / (1024 ** 3),
            )
        )

    lines.extend(
        [
            "",
            "## Reorg Decisions",
            "",
            "| variant | runs | avg accepted plans | max net benefit | last tau |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    for variant, group in sorted(reorg_by_variant.items()):
        lines.append(
            "| {} | {} | {:.2f} | {:.3f} | {:.3f} |".format(
                variant,
                len(group),
                mean([to_float(r.get("accepted_plans")) for r in group]),
                max([to_float(r.get("max_net_benefit")) for r in group], default=0.0),
                mean([to_float(r.get("last_tau_trigger")) for r in group]),
            )
        )

    out_md.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: summarize_m3_results.py RESULT_ROOT OUT_MD", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    out_md = Path(sys.argv[2])
    runtime_rows = collect_runtime_rows(root)
    reorg_rows = collect_reorg_rows(root)
    write_csv(out_md.with_name("m3_run_metrics.csv"), runtime_rows)
    write_csv(out_md.with_name("m3_reorg_trace_summary.csv"), reorg_rows)
    write_markdown(root, out_md, runtime_rows, reorg_rows)
    print(f"Wrote {out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
