#!/usr/bin/env python3
"""Aggregate FACO M3 compare results.

The script reads the result tree produced by run_reorg_compare.sh and writes a
small Markdown report plus CSV sidecars for runtime and reorg trace metrics.
"""

from __future__ import annotations

import csv
import re
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


def parse_reorg_debug(path: Path) -> dict[str, str]:
    """Parse ReorgPlanner{key=value,...} summary files."""
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"ReorgPlanner\{(?P<body>.*)\}", text)
    if not match:
        return {}
    values: dict[str, str] = {}
    for part in match.group("body").split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
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
        warmup = [row for row in samples if int(to_float(row.get("warmup"))) == 1]
        rate_limited = [
            row for row in samples if int(to_float(row.get("rate_limited"))) == 1
        ]
        debug = parse_reorg_debug(run_dir / "faco_reorg_summary.txt")
        adaptive_q_values = [
            to_float(row.get("adaptive_q")) for row in samples if row.get("adaptive_q")
        ]
        adaptive_tau_values = [
            to_float(row.get("adaptive_tau"))
            for row in samples
            if row.get("adaptive_tau")
        ]
        rows.append(
            {
                "variant": variant,
                "run": run_name,
                "samples": str(len(samples)),
                "tau_mode": debug.get("tau_mode", samples[-1].get("tau_mode", "") if samples else ""),
                "eval_count": debug.get("eval_count", ""),
                "no_candidate_count": debug.get("no_candidate_count", ""),
                "rejected_plans": debug.get("rejected_plans", ""),
                "accepted_plans": str(len(accepted)),
                "accepted_total": debug.get("accepted_plans", ""),
                "executed_plans": debug.get("executed_plans", ""),
                "migrated_extents": debug.get("migrated_extents", ""),
                "migrated_bytes": debug.get("migrated_bytes", ""),
                "cooldown_skip_count": debug.get("cooldown_skip_count", ""),
                "tiny_plan_skip_count": debug.get("tiny_plan_skip_count", ""),
                "warmup_reject_count": debug.get(
                    "warmup_reject_count", str(len(warmup))
                ),
                "rate_limited_reject_count": debug.get(
                    "rate_limited_reject_count", str(len(rate_limited))
                ),
                "adaptive_q_avg": f"{mean(adaptive_q_values):.6f}",
                "adaptive_tau_last": f"{adaptive_tau_values[-1] if adaptive_tau_values else 0.0:.3f}",
                "history_size_last": samples[-1].get("history_size", "0") if samples else "0",
                "max_net_benefit": f"{max([to_float(r.get('net_benefit')) for r in samples], default=0.0):.3f}",
                "max_net_seen": debug.get("max_net_seen", ""),
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


def validity_status(runtime_rows: list[dict[str, str]], reorg_rows: list[dict[str, str]]) -> str:
    """Classify whether the M3 result actually exercised the decision path."""
    reorg_on_rows = [row for row in reorg_rows if row.get("variant") == "reorg_on"]
    runtime_on_rows = [
        row for row in runtime_rows if row.get("variant") == "reorg_on"
    ]
    if not reorg_on_rows:
        return "FAIL: M3 reorg artifacts missing"

    samples = sum(to_float(row.get("samples")) for row in reorg_on_rows)
    accepted = sum(
        to_float(row.get("accepted_total") or row.get("accepted_plans"))
        for row in reorg_on_rows
    )
    executed = sum(to_float(row.get("executed_plans")) for row in reorg_on_rows)
    migrated_bytes = sum(
        to_float(row.get("migrated_bytes")) for row in reorg_on_rows
    )
    gc_bytes = sum(to_float(row.get("gc_bytes_written")) for row in runtime_on_rows)

    if samples == 0:
        return "FAIL: M3 decision path not exercised"
    if accepted == 0:
        return "PARAMETER_ONLY: M3 evaluated but never acted"
    if executed == 0 or migrated_bytes == 0 or gc_bytes == 0:
        return "EXECUTION_BUG_OR_NOOP: M3 accepted but did not migrate"
    return "ACTIONABLE: M3 evaluated and migrated data"


def write_markdown(root: Path, out_md: Path, runtime_rows: list[dict[str, str]], reorg_rows: list[dict[str, str]]) -> None:
    """Write the M3 rollup report."""
    runtime_by_variant = group_by_variant(runtime_rows)
    reorg_by_variant = group_by_variant(reorg_rows)
    validity = validity_status(runtime_rows, reorg_rows)
    lines = [
        "# M3 reorg summary",
        "",
        f"- result root: `{root}`",
        f"- runtime rows: `{len(runtime_rows)}`",
        f"- reorg trace rows: `{len(reorg_rows)}`",
        f"- validity: `{validity}`",
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
            "| variant | mode | runs | avg evals | avg rejected | avg accepted | avg executed | migrated MB | cooldown skips | tiny skips | warmup rejects | rate rejects | avg q | last adaptive tau | max net | last tau |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for variant, group in sorted(reorg_by_variant.items()):
        modes = sorted({r.get("tau_mode", "") for r in group if r.get("tau_mode", "")})
        mode = "/".join(modes) if modes else ""
        lines.append(
            "| {} | {} | {} | {:.2f} | {:.2f} | {:.2f} | {:.2f} | {:.3f} | {:.2f} | {:.2f} | {:.2f} | {:.2f} | {:.3f} | {:.3f} | {:.3f} | {:.3f} |".format(
                variant,
                mode,
                len(group),
                mean([to_float(r.get("eval_count")) for r in group]),
                mean([to_float(r.get("rejected_plans")) for r in group]),
                mean(
                    [
                        to_float(r.get("accepted_total") or r.get("accepted_plans"))
                        for r in group
                    ]
                ),
                mean([to_float(r.get("executed_plans")) for r in group]),
                mean([to_float(r.get("migrated_bytes")) for r in group])
                / (1024 ** 2),
                mean([to_float(r.get("cooldown_skip_count")) for r in group]),
                mean([to_float(r.get("tiny_plan_skip_count")) for r in group]),
                mean([to_float(r.get("warmup_reject_count")) for r in group]),
                mean([to_float(r.get("rate_limited_reject_count")) for r in group]),
                mean([to_float(r.get("adaptive_q_avg")) for r in group]),
                mean([to_float(r.get("adaptive_tau_last")) for r in group]),
                max(
                    [
                        to_float(r.get("max_net_seen") or r.get("max_net_benefit"))
                        for r in group
                    ],
                    default=0.0,
                ),
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
