#!/usr/bin/env python3
"""Summarize FACO M2 compare or sensitivity result directories.

Typical usage:
  python3 experiments/M2/summarize_m2_results.py \
    experiments/M2/results/<result-dir> \
    experiments/M2/results/<result-dir>/m2_wrapup_summary.md
"""

from __future__ import annotations

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def parse_kv(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def to_float(value: str | None, default: float = 0.0) -> float:
    try:
        return float(value if value not in (None, "") else default)
    except ValueError:
        return default


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def collect_summary_rows(root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for summary in sorted(root.rglob("summary.csv")):
        scenario = summary.parent.relative_to(root).as_posix()
        if scenario == ".":
            scenario = "default"
        with summary.open(newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                row["scenario"] = scenario
                row["summary_dir"] = str(summary.parent)
                rows.append(row)
    return rows


def collect_run_metrics(root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for cfsm in sorted(root.rglob("faco_cfsm_summary.txt")):
        run_dir = cfsm.parent
        variant = run_dir.parent.name
        run_id = run_dir.name
        rel_parts = run_dir.relative_to(root).parts
        scenario = "/".join(rel_parts[:-2]) if len(rel_parts) >= 3 else "default"
        cfsm_values = parse_kv(cfsm)
        runtime = parse_kv(run_dir / "faco_runtime_metrics.txt")

        row = {
            "scenario": scenario,
            "variant": variant,
            "run": run_id,
            "run_dir": str(run_dir),
            "active_zones": cfsm_values.get("active_zones", ""),
            "empty_zones": cfsm_values.get("empty_zones", ""),
            "high_fragment_zones": str(
                int(to_float(cfsm_values.get("class_cold_high")))
                + int(to_float(cfsm_values.get("class_hot_high")))
            ),
            "total_valid_bytes": cfsm_values.get("total_valid_bytes", ""),
            "zone_reset_count": runtime.get("zone_reset_count", "NA"),
            "zone_finish_count": runtime.get("zone_finish_count", "NA"),
            "bytes_written": runtime.get("bytes_written", "NA"),
            "gc_bytes_written": runtime.get("gc_bytes_written", "NA"),
            "user_bytes_written": runtime.get("user_bytes_written", "NA"),
        }
        rows.append(row)
    return rows


def collect_budget_trace_rows(root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for trace in sorted(root.rglob("faco_budget_trace.csv")):
        rel_parts = trace.parent.relative_to(root).parts
        scenario = "/".join(rel_parts[:-2]) if len(rel_parts) >= 3 else "default"
        with trace.open(newline="", encoding="utf-8") as fh:
            samples = list(csv.DictReader(fh))
        if not samples:
            continue
        budgets = [to_float(row.get("budget")) for row in samples]
        pressures = [to_float(row.get("p_frag")) for row in samples]
        rows.append(
            {
                "scenario": scenario,
                "run_dir": str(trace.parent),
                "samples": str(len(samples)),
                "min_budget": f"{min(budgets):.0f}",
                "max_budget": f"{max(budgets):.0f}",
                "last_budget": f"{budgets[-1]:.0f}",
                "avg_p_frag": f"{mean(pressures):.6f}",
                "max_p_frag": f"{max(pressures):.6f}",
            }
        )
    return rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    keys = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    out = ["| " + " | ".join(headers) + " |"]
    out.append("|" + "|".join("---" for _ in headers) + "|")
    for row in rows:
        out.append("| " + " | ".join(row) + " |")
    return out


def summarize_throughput(rows: list[dict[str, str]]) -> list[list[str]]:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row.get("benchmark") == "ERROR":
            continue
        grouped[
            (
                row.get("scenario", "default"),
                row.get("variant", ""),
                row.get("benchmark", ""),
            )
        ].append(row)

    out: list[list[str]] = []
    for (scenario, variant, benchmark), group in sorted(grouped.items()):
        out.append(
            [
                scenario,
                variant,
                benchmark,
                str(len(group)),
                f"{mean([to_float(r.get('micros_per_op')) for r in group]):.3f}",
                f"{mean([to_float(r.get('ops_per_sec')) for r in group]):.0f}",
                f"{mean([to_float(r.get('mb_per_sec')) for r in group]):.3f}",
            ]
        )
    return out


def summarize_run_metrics(rows: list[dict[str, str]]) -> list[list[str]]:
    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[
            (row.get("scenario", "default"), row.get("variant", ""))
        ].append(row)

    out: list[list[str]] = []
    for (scenario, variant), group in sorted(grouped.items()):
        out.append(
            [
                scenario,
                variant,
                str(len(group)),
                f"{mean([to_float(r.get('active_zones')) for r in group]):.2f}",
                f"{mean([to_float(r.get('empty_zones')) for r in group]):.2f}",
                f"{mean([to_float(r.get('high_fragment_zones')) for r in group]):.2f}",
                f"{mean([to_float(r.get('zone_reset_count')) for r in group if r.get('zone_reset_count') != 'NA']):.2f}"
                if any(r.get("zone_reset_count") != "NA" for r in group)
                else "NA",
                f"{mean([to_float(r.get('zone_finish_count')) for r in group if r.get('zone_finish_count') != 'NA']):.2f}"
                if any(r.get("zone_finish_count") != "NA" for r in group)
                else "NA",
                f"{mean([to_float(r.get('gc_bytes_written')) for r in group if r.get('gc_bytes_written') != 'NA']) / (1024 ** 3):.3f}"
                if any(r.get("gc_bytes_written") != "NA" for r in group)
                else "NA",
            ]
        )
    return out


def metric_average(rows: list[dict[str, str]], key: str) -> float:
    values = [to_float(row.get(key)) for row in rows if row.get(key) not in ("", "NA")]
    return mean(values)


def pct_delta(value: float, baseline: float) -> str:
    if baseline == 0:
        return "NA"
    return f"{((value - baseline) / baseline) * 100:.2f}%"


def summarize_throughput_deltas(rows: list[dict[str, str]]) -> list[list[str]]:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row.get("benchmark") == "ERROR":
            continue
        grouped[
            (
                row.get("scenario", "default"),
                row.get("variant", ""),
                row.get("benchmark", ""),
            )
        ].append(row)

    baseline: dict[str, float] = {}
    for (_scenario, variant, benchmark), group in grouped.items():
        if variant == "budget_off":
            baseline.setdefault(
                benchmark,
                mean([to_float(row.get("mb_per_sec")) for row in group]),
            )

    out: list[list[str]] = []
    for (scenario, variant, benchmark), group in sorted(grouped.items()):
        if variant != "budget_on" or benchmark not in baseline:
            continue
        value = mean([to_float(row.get("mb_per_sec")) for row in group])
        base = baseline[benchmark]
        out.append(
            [
                scenario,
                benchmark,
                f"{value:.3f}",
                f"{base:.3f}",
                pct_delta(value, base),
            ]
        )
    return out


def summarize_runtime_deltas(rows: list[dict[str, str]]) -> list[list[str]]:
    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[(row.get("scenario", "default"), row.get("variant", ""))].append(row)

    baseline_groups = [
        group for (_scenario, variant), group in grouped.items() if variant == "budget_off"
    ]
    if not baseline_groups:
        return []
    baseline_rows = [row for group in baseline_groups for row in group]
    baseline = {
        "active_zones": metric_average(baseline_rows, "active_zones"),
        "high_fragment_zones": metric_average(baseline_rows, "high_fragment_zones"),
        "zone_reset_count": metric_average(baseline_rows, "zone_reset_count"),
        "zone_finish_count": metric_average(baseline_rows, "zone_finish_count"),
    }

    out: list[list[str]] = []
    for (scenario, variant), group in sorted(grouped.items()):
        if variant != "budget_on":
            continue
        active = metric_average(group, "active_zones")
        high = metric_average(group, "high_fragment_zones")
        resets = metric_average(group, "zone_reset_count")
        finishes = metric_average(group, "zone_finish_count")
        out.append(
            [
                scenario,
                f"{active:.2f}",
                pct_delta(active, baseline["active_zones"]),
                f"{high:.2f}",
                pct_delta(high, baseline["high_fragment_zones"]),
                f"{resets:.2f}",
                pct_delta(resets, baseline["zone_reset_count"]),
                f"{finishes:.2f}",
                pct_delta(finishes, baseline["zone_finish_count"]),
            ]
        )
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: summarize_m2_results.py RESULT_ROOT OUT_MD", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    out_md = Path(sys.argv[2])
    out_md.parent.mkdir(parents=True, exist_ok=True)

    throughput_rows = collect_summary_rows(root)
    run_metrics = collect_run_metrics(root)
    budget_rows = collect_budget_trace_rows(root)

    write_csv(out_md.with_name("m2_run_metrics.csv"), run_metrics)
    write_csv(out_md.with_name("m2_budget_trace_summary.csv"), budget_rows)

    lines: list[str] = [
        "# M2 wrap-up summary",
        "",
        f"- result root: `{root}`",
        f"- throughput rows: `{len(throughput_rows)}`",
        f"- run metric rows: `{len(run_metrics)}`",
        f"- budget trace rows: `{len(budget_rows)}`",
        "",
        "## Throughput",
        "",
    ]
    lines += markdown_table(
        [
            "scenario",
            "variant",
            "benchmark",
            "rows",
            "avg micros/op",
            "avg ops/sec",
            "avg MB/s",
        ],
        summarize_throughput(throughput_rows),
    )

    lines += [
        "",
        "## Fragmentation And Runtime Metrics",
        "",
    ]
    lines += markdown_table(
        [
            "scenario",
            "variant",
            "runs",
            "avg active zones",
            "avg empty zones",
            "avg high-frag zones",
            "avg resets",
            "avg finishes",
            "avg GC GB",
        ],
        summarize_run_metrics(run_metrics),
    )

    throughput_deltas = summarize_throughput_deltas(throughput_rows)
    runtime_deltas = summarize_runtime_deltas(run_metrics)
    if throughput_deltas or runtime_deltas:
        lines += [
            "",
            "## Baseline Comparison",
            "",
        ]
        if throughput_deltas:
            lines += markdown_table(
                [
                    "scenario",
                    "benchmark",
                    "budget_on MB/s",
                    "budget_off MB/s",
                    "delta",
                ],
                throughput_deltas,
            )
        if runtime_deltas:
            lines += [
                "",
            ]
            lines += markdown_table(
                [
                    "scenario",
                    "active zones",
                    "active delta",
                    "high-frag zones",
                    "high-frag delta",
                    "resets",
                    "reset delta",
                    "finishes",
                    "finish delta",
                ],
                runtime_deltas,
            )

    lines += [
        "",
        "## Budget Traces",
        "",
    ]
    lines += markdown_table(
        [
            "run dir",
            "scenario",
            "samples",
            "min budget",
            "max budget",
            "last budget",
            "avg p_frag",
            "max p_frag",
        ],
        [
            [
                row["run_dir"],
                row["scenario"],
                row["samples"],
                row["min_budget"],
                row["max_budget"],
                row["last_budget"],
                row["avg_p_frag"],
                row["max_p_frag"],
            ]
            for row in budget_rows
        ],
    )

    lines += [
        "",
        "## Notes",
        "",
        "- `NA` means the run was produced before `faco_runtime_metrics.txt` export was available.",
        "- M2 is expected to reduce active/high-fragment zones; throughput is a guardrail, not the only target.",
        "",
    ]

    out_md.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {out_md}")
    print(f"Wrote {out_md.with_name('m2_run_metrics.csv')}")
    print(f"Wrote {out_md.with_name('m2_budget_trace_summary.csv')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
