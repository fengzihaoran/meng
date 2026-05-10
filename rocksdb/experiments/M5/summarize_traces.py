#!/usr/bin/env python3
"""Summarize FACO M5 artifacts across M1-M4/M5 result trees.

This script is intentionally offline-only: it reads copied FACO trace files and
writes CSV/Markdown summaries. It does not run db_bench or touch a ZNS device.
"""

from __future__ import annotations

import csv
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def to_float(value: str | None, default: float = 0.0) -> float:
    if value in (None, "", "NA"):
        return default
    try:
        return float(value)
    except ValueError:
        match = re.match(r"[-+]?[0-9]*\.?[0-9]+", value)
        return float(match.group(0)) if match else default


def to_optional_float(value: str | None) -> float | None:
    if value in (None, "", "NA"):
        return None
    try:
        return float(value)
    except ValueError:
        match = re.match(r"[-+]?[0-9]*\.?[0-9]+", value)
        return float(match.group(0)) if match else None


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def sample_stddev(values: list[float]) -> float | None:
    if len(values) < 2:
        return None
    return statistics.stdev(values)


def t95_critical(df: int) -> float:
    table = {
        1: 12.706,
        2: 4.303,
        3: 3.182,
        4: 2.776,
        5: 2.571,
        6: 2.447,
        7: 2.365,
        8: 2.306,
        9: 2.262,
        10: 2.228,
        11: 2.201,
        12: 2.179,
        13: 2.160,
        14: 2.145,
        15: 2.131,
        16: 2.120,
        17: 2.110,
        18: 2.101,
        19: 2.093,
        20: 2.086,
        21: 2.080,
        22: 2.074,
        23: 2.069,
        24: 2.064,
        25: 2.060,
        26: 2.056,
        27: 2.052,
        28: 2.048,
        29: 2.045,
        30: 2.042,
    }
    if df <= 0:
        return 0.0
    if df <= 30:
        return table[df]
    return 1.960


def ci95_half_width(values: list[float]) -> float | None:
    stddev = sample_stddev(values)
    if stddev is None:
        return None
    return t95_critical(len(values) - 1) * stddev / (len(values) ** 0.5)


def stat_cells(values: list[float], digits: int = 3) -> tuple[str, str, str, str]:
    if not values:
        return "0", "NA", "NA", "NA"
    avg = statistics.fmean(values)
    stddev = sample_stddev(values)
    ci95 = ci95_half_width(values)
    fmt = f"{{:.{digits}f}}"
    return (
        str(len(values)),
        fmt.format(avg),
        fmt.format(stddev) if stddev is not None else "NA",
        fmt.format(ci95) if ci95 is not None else "NA",
    )


def numeric_values(rows: list[dict[str, str]], key: str, scale: float = 1.0) -> list[float]:
    values: list[float] = []
    for row in rows:
        value = to_optional_float(row.get(key))
        if value is not None:
            values.append(value / scale)
    return values


def parse_kv_lines(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def parse_brace_debug(path: Path, prefix: str) -> dict[str, str]:
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(re.escape(prefix) + r"\{(?P<body>.*)\}", text)
    if not match:
        return {}
    values: dict[str, str] = {}
    for part in match.group("body").split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8", errors="replace") as fh:
        return list(csv.DictReader(fh))


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def run_identity(run_dir: Path, root: Path) -> tuple[str, str, str]:
    parts = run_dir.relative_to(root).parts
    run = run_dir.name
    variant = run_dir.parent.name if run.startswith("run_") else "default"
    experiment = "/".join(parts[:-2]) if run.startswith("run_") and len(parts) >= 3 else "."
    return experiment, variant, run


def find_run_dirs(root: Path) -> list[Path]:
    marker_names = {
        "faco_runtime_metrics.txt",
        "faco_cfsm_summary.txt",
        "faco_reorg_trace.csv",
        "faco_lacr_trace.csv",
        "faco_metrics.txt",
        "db_bench.log",
    }
    dirs: set[Path] = set()
    for marker in marker_names:
        for path in root.rglob(marker):
            dirs.add(path.parent)
    return sorted(dirs)


def parse_faco_metrics(path: Path) -> dict[str, str]:
    metrics: dict[str, str] = {}
    if not path.exists():
        return metrics
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        name = fields[0]
        for field in fields[1:]:
            if field.startswith("value="):
                metrics[name] = field.split("=", 1)[1]
    return metrics


def summarize_lacr_trace(rows: list[dict[str, str]]) -> dict[str, str]:
    if not rows:
        return {
            "lacr_compaction_events": "0",
            "lacr_compaction_input_bytes": "0",
            "lacr_compaction_output_bytes": "0",
            "lacr_touched_zone_count": "0",
            "lacr_active_files_max": "0",
        }
    touched: set[str] = set()
    input_bytes = 0.0
    output_bytes = 0.0
    active_files_max = 0.0
    for row in rows:
        input_bytes += to_float(row.get("total_input_bytes"))
        output_bytes += to_float(row.get("total_output_bytes"))
        active_files_max = max(active_files_max, to_float(row.get("active_compaction_files")))
        for zone in (row.get("touched_zones") or "").split(";"):
            if zone:
                touched.add(zone)
    return {
        "lacr_compaction_events": str(len(rows)),
        "lacr_compaction_input_bytes": f"{input_bytes:.0f}",
        "lacr_compaction_output_bytes": f"{output_bytes:.0f}",
        "lacr_touched_zone_count": str(len(touched)),
        "lacr_active_files_max": f"{active_files_max:.0f}",
    }


def summarize_reorg_trace(rows: list[dict[str, str]]) -> dict[str, str]:
    if not rows:
        return {
            "reorg_trace_samples": "0",
            "reorg_accepted_samples": "0",
            "reorg_duplicate_movement_bytes_proxy": "0",
            "reorg_lacr_rows": "0",
        }
    accepted = [row for row in rows if to_float(row.get("accepted")) > 0]
    lacr_rows = [row for row in rows if to_float(row.get("lacr_enabled")) > 0]
    duplicate_proxy = sum(
        to_float(row.get("estimated_valid_bytes"))
        for row in accepted
        if to_float(row.get("compaction_touched_zone")) > 0
    )
    return {
        "reorg_trace_samples": str(len(rows)),
        "reorg_accepted_samples": str(len(accepted)),
        "reorg_duplicate_movement_bytes_proxy": f"{duplicate_proxy:.0f}",
        "reorg_lacr_rows": str(len(lacr_rows)),
        "reorg_lacr_synergy_bonus_avg": f"{mean([to_float(r.get('lacr_synergy_bonus')) for r in lacr_rows]):.3f}",
        "reorg_lacr_waste_penalty_avg": f"{mean([to_float(r.get('lacr_waste_penalty')) for r in lacr_rows]):.3f}",
        "reorg_lacr_latency_penalty_avg": f"{mean([to_float(r.get('lacr_latency_penalty')) for r in lacr_rows]):.3f}",
    }


def collect_run_rows(root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for run_dir in find_run_dirs(root):
        experiment, variant, run = run_identity(run_dir, root)
        runtime = parse_kv_lines(run_dir / "faco_runtime_metrics.txt")
        cfsm = parse_kv_lines(run_dir / "faco_cfsm_summary.txt")
        reorg_debug = parse_brace_debug(run_dir / "faco_reorg_summary.txt", "ReorgPlanner")
        lacr_debug = parse_brace_debug(run_dir / "faco_lacr_summary.txt", "FacoLacrState")
        metrics = parse_faco_metrics(run_dir / "faco_metrics.txt")
        reorg_trace = summarize_reorg_trace(read_csv(run_dir / "faco_reorg_trace.csv"))
        lacr_trace = summarize_lacr_trace(read_csv(run_dir / "faco_lacr_trace.csv"))

        total_bytes = to_float(runtime.get("bytes_written"))
        user_bytes = to_float(runtime.get("user_bytes_written"))
        gc_bytes = to_float(runtime.get("gc_bytes_written"))
        reorg_bytes = to_float(reorg_debug.get("migrated_bytes"))
        compaction_bytes = to_float(lacr_trace.get("lacr_compaction_output_bytes"))
        total_movement = reorg_bytes + compaction_bytes

        has_high_frag_classes = (
            "class_cold_high" in cfsm or "class_hot_high" in cfsm
        )
        cold_high = to_float(cfsm.get("class_cold_high"))
        hot_high = to_float(cfsm.get("class_hot_high"))

        row = {
            "experiment": experiment,
            "variant": variant,
            "run": run,
            "run_dir": str(run_dir),
            "zone_reset_count": runtime.get("zone_reset_count", ""),
            "zone_finish_count": runtime.get("zone_finish_count", ""),
            "bytes_written": runtime.get("bytes_written", ""),
            "gc_bytes_written": runtime.get("gc_bytes_written", ""),
            "user_bytes_written": runtime.get("user_bytes_written", ""),
            "write_amplification": f"{total_bytes / user_bytes:.6f}" if user_bytes > 0 else "NA",
            "active_zones": cfsm.get("active_zones", ""),
            "empty_zones": cfsm.get("empty_zones", ""),
            "high_frag_zones": (
                f"{cold_high + hot_high:.0f}" if has_high_frag_classes else "NA"
            ),
            "faco_frag_zvdr_p99": metrics.get("faco.frag.zvdr.p99", ""),
            "faco_frag_invalid_ratio_p99": metrics.get("faco.frag.invalid_ratio.p99", ""),
            "faco_frag_rbd_max": metrics.get("faco.frag.rbd.max", ""),
            "reorg_executed_plans": reorg_debug.get("executed_plans", ""),
            "reorg_migrated_bytes": reorg_debug.get("migrated_bytes", ""),
            "reorg_rejected_plans": reorg_debug.get("rejected_plans", ""),
            "reorg_tau_effective": reorg_debug.get("tau_effective", ""),
            "lacr_active_compaction_files": lacr_debug.get("active_compaction_files", ""),
            "total_movement_bytes_proxy": f"{total_movement:.0f}",
            "movement_compaction_bytes_proxy": f"{compaction_bytes:.0f}",
            "movement_reorg_bytes": f"{reorg_bytes:.0f}",
            "natural_aging_resets_total": metrics.get("faco.lacr.natural_aging_resets_total", "NA"),
            "lifetime_prediction_error_pct": metrics.get("faco.lacr.lifetime_prediction_error_pct", "NA"),
        }
        row.update(reorg_trace)
        row.update(lacr_trace)
        rows.append(row)
    return rows


def add_metric_stats(
    row: dict[str, str],
    rows: list[dict[str, str]],
    prefix: str,
    key: str,
    *,
    scale: float = 1.0,
    digits: int = 3,
) -> None:
    n, avg, stddev, ci95 = stat_cells(numeric_values(rows, key, scale), digits)
    row[f"{prefix}_n"] = n
    row[f"{prefix}_mean"] = avg
    row[f"{prefix}_std"] = stddev
    row[f"{prefix}_ci95"] = ci95


def grouped_runtime_eval_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[(row.get("experiment", "."), row.get("variant", ""))].append(row)

    out: list[dict[str, str]] = []
    for (experiment, variant), group in sorted(grouped.items()):
        row = {
            "experiment": experiment,
            "variant": variant,
            "runs": str(len(group)),
        }
        add_metric_stats(
            row, group, "wa", "write_amplification", digits=6
        )
        add_metric_stats(row, group, "zone_resets", "zone_reset_count")
        add_metric_stats(row, group, "high_frag_zones", "high_frag_zones")
        add_metric_stats(
            row,
            group,
            "frag_invalid_ratio_p99",
            "faco_frag_invalid_ratio_p99",
            digits=6,
        )
        add_metric_stats(
            row, group, "frag_zvdr_p99", "faco_frag_zvdr_p99", digits=6
        )
        add_metric_stats(
            row,
            group,
            "reorg_mb",
            "movement_reorg_bytes",
            scale=1024 ** 2,
        )
        add_metric_stats(
            row,
            group,
            "movement_mb",
            "total_movement_bytes_proxy",
            scale=1024 ** 2,
        )
        out.append(row)
    return out


def grouped_db_bench_eval_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        benchmark = row.get("benchmark", "")
        if not benchmark or benchmark == "ERROR":
            continue
        grouped[
            (
                row.get("experiment", "."),
                row.get("variant", row.get("summary_scope", "")),
                benchmark,
            )
        ].append(row)

    out: list[dict[str, str]] = []
    for (experiment, variant, benchmark), group in sorted(grouped.items()):
        row = {
            "experiment": experiment,
            "variant": variant,
            "benchmark": benchmark,
            "rows": str(len(group)),
        }
        add_metric_stats(row, group, "ops_per_sec", "ops_per_sec", digits=3)
        add_metric_stats(row, group, "mb_per_sec", "mb_per_sec", digits=3)
        add_metric_stats(
            row, group, "micros_per_op", "micros_per_op", digits=3
        )
        out.append(row)
    return out


def format_optional(value: float | None, digits: int = 3) -> str:
    if value is None:
        return "NA"
    return f"{value:.{digits}f}"


def mean_from_eval(row: dict[str, str] | None, prefix: str) -> float | None:
    if row is None:
        return None
    return to_optional_float(row.get(f"{prefix}_mean"))


def stat_from_eval(row: dict[str, str] | None, prefix: str, suffix: str) -> str:
    if row is None:
        return "NA"
    return row.get(f"{prefix}_{suffix}", "NA") or "NA"


def direction_improvement_pct(
    candidate: float | None,
    baseline: float | None,
    direction: str,
) -> float | None:
    if candidate is None or baseline is None or baseline == 0:
        return None
    if direction == "higher_is_better":
        return (candidate - baseline) / baseline * 100.0
    if direction == "lower_is_better":
        return (baseline - candidate) / baseline * 100.0
    return None


def raw_delta_pct(candidate: float | None, baseline: float | None) -> float | None:
    if candidate is None or baseline is None or baseline == 0:
        return None
    return (candidate - baseline) / baseline * 100.0


def evidence_label(improvement_pct: float | None) -> str:
    if improvement_pct is None:
        return "not_comparable"
    if improvement_pct > 0.0:
        return "improves"
    if improvement_pct < 0.0:
        return "regresses"
    return "ties"


def comparison_row(
    *,
    comparison_scope: str,
    experiment: str,
    metric_family: str,
    metric: str,
    direction: str,
    baseline_variant: str,
    candidate_variant: str,
    baseline_row: dict[str, str] | None,
    candidate_row: dict[str, str] | None,
    prefix: str,
    benchmark: str = "",
    claim_use: str,
) -> dict[str, str]:
    baseline_mean = mean_from_eval(baseline_row, prefix)
    candidate_mean = mean_from_eval(candidate_row, prefix)
    improvement = direction_improvement_pct(candidate_mean, baseline_mean, direction)
    delta = (
        candidate_mean - baseline_mean
        if candidate_mean is not None and baseline_mean is not None
        else None
    )
    status = "ok" if improvement is not None else "not_comparable"
    return {
        "comparison_scope": comparison_scope,
        "experiment": experiment,
        "metric_family": metric_family,
        "benchmark": benchmark,
        "metric": metric,
        "direction": direction,
        "baseline_variant": baseline_variant,
        "candidate_variant": candidate_variant,
        "baseline_n": stat_from_eval(baseline_row, prefix, "n"),
        "baseline_mean": format_optional(baseline_mean, 6),
        "baseline_std": stat_from_eval(baseline_row, prefix, "std"),
        "baseline_ci95": stat_from_eval(baseline_row, prefix, "ci95"),
        "candidate_n": stat_from_eval(candidate_row, prefix, "n"),
        "candidate_mean": format_optional(candidate_mean, 6),
        "candidate_std": stat_from_eval(candidate_row, prefix, "std"),
        "candidate_ci95": stat_from_eval(candidate_row, prefix, "ci95"),
        "delta": format_optional(delta, 6),
        "delta_pct": format_optional(raw_delta_pct(candidate_mean, baseline_mean), 3),
        "improvement_pct": format_optional(improvement, 3),
        "evidence": evidence_label(improvement),
        "status": status,
        "claim_use": claim_use,
    }


def build_eval_indexes(
    runtime_eval_rows: list[dict[str, str]],
    db_bench_eval_rows: list[dict[str, str]],
) -> tuple[
    dict[tuple[str, str], dict[str, str]],
    dict[tuple[str, str, str], dict[str, str]],
]:
    runtime = {
        (row.get("experiment", ""), row.get("variant", "")): row
        for row in runtime_eval_rows
    }
    bench = {
        (
            row.get("experiment", ""),
            row.get("variant", ""),
            row.get("benchmark", ""),
        ): row
        for row in db_bench_eval_rows
    }
    return runtime, bench


def exp4_benchmarks(
    bench_index: dict[tuple[str, str, str], dict[str, str]],
) -> list[str]:
    return sorted(
        benchmark
        for experiment, variant, benchmark in bench_index
        if experiment == "exp4" and variant == "full_faco" and benchmark
    )


def grouped_baseline_comparison_rows(
    runtime_eval_rows: list[dict[str, str]],
    db_bench_eval_rows: list[dict[str, str]],
) -> list[dict[str, str]]:
    runtime_index, bench_index = build_eval_indexes(runtime_eval_rows, db_bench_eval_rows)
    rows: list[dict[str, str]] = []
    experiment = "exp4"

    for benchmark in exp4_benchmarks(bench_index):
        baseline = bench_index.get((experiment, "native", benchmark))
        candidate = bench_index.get((experiment, "full_faco", benchmark))
        for metric, prefix, direction in [
            ("ops_per_sec", "ops_per_sec", "higher_is_better"),
            ("mb_per_sec", "mb_per_sec", "higher_is_better"),
            ("micros_per_op", "micros_per_op", "lower_is_better"),
        ]:
            rows.append(
                comparison_row(
                    comparison_scope="performance_baseline",
                    experiment=experiment,
                    metric_family="db_bench",
                    benchmark=benchmark,
                    metric=metric,
                    direction=direction,
                    baseline_variant="native",
                    candidate_variant="full_faco",
                    baseline_row=baseline,
                    candidate_row=candidate,
                    prefix=prefix,
                    claim_use="native is performance-only baseline",
                )
            )

    baseline = runtime_index.get((experiment, "cfsm_only"))
    candidate = runtime_index.get((experiment, "full_faco"))
    for metric, prefix, direction in [
        ("write_amplification", "wa", "lower_is_better"),
        ("zone_reset_count", "zone_resets", "lower_is_better"),
        ("high_frag_zones", "high_frag_zones", "lower_is_better"),
        ("frag_invalid_ratio_p99", "frag_invalid_ratio_p99", "lower_is_better"),
        ("frag_zvdr_p99", "frag_zvdr_p99", "lower_is_better"),
    ]:
        rows.append(
            comparison_row(
                comparison_scope="instrumented_fragmentation_baseline",
                experiment=experiment,
                metric_family="runtime",
                metric=metric,
                direction=direction,
                baseline_variant="cfsm_only",
                candidate_variant="full_faco",
                baseline_row=baseline,
                candidate_row=candidate,
                prefix=prefix,
                claim_use="cfsm_only is fragmentation-observable baseline",
            )
        )
    return rows


def grouped_ablation_comparison_rows(
    runtime_eval_rows: list[dict[str, str]],
    db_bench_eval_rows: list[dict[str, str]],
) -> list[dict[str, str]]:
    runtime_index, bench_index = build_eval_indexes(runtime_eval_rows, db_bench_eval_rows)
    rows: list[dict[str, str]] = []
    experiment = "exp4"
    ablations = ["native", "cfsm_only", "without_ebcr", "without_lacr"]

    for baseline_variant in ablations:
        baseline = runtime_index.get((experiment, baseline_variant))
        candidate = runtime_index.get((experiment, "full_faco"))
        for metric, prefix, direction in [
            ("write_amplification", "wa", "lower_is_better"),
            ("zone_reset_count", "zone_resets", "lower_is_better"),
            ("high_frag_zones", "high_frag_zones", "lower_is_better"),
        ]:
            rows.append(
                comparison_row(
                    comparison_scope="ablation_runtime",
                    experiment=experiment,
                    metric_family="runtime",
                    metric=metric,
                    direction=direction,
                    baseline_variant=baseline_variant,
                    candidate_variant="full_faco",
                    baseline_row=baseline,
                    candidate_row=candidate,
                    prefix=prefix,
                    claim_use=(
                        "native runtime FACO-local metrics are not comparable"
                        if baseline_variant == "native"
                        else "FACO-internal ablation"
                    ),
                )
            )

        for benchmark in exp4_benchmarks(bench_index):
            baseline_bench = bench_index.get((experiment, baseline_variant, benchmark))
            candidate_bench = bench_index.get((experiment, "full_faco", benchmark))
            for metric, prefix, direction in [
                ("ops_per_sec", "ops_per_sec", "higher_is_better"),
                ("mb_per_sec", "mb_per_sec", "higher_is_better"),
                ("micros_per_op", "micros_per_op", "lower_is_better"),
            ]:
                rows.append(
                    comparison_row(
                        comparison_scope="ablation_performance",
                        experiment=experiment,
                        metric_family="db_bench",
                        benchmark=benchmark,
                        metric=metric,
                        direction=direction,
                        baseline_variant=baseline_variant,
                        candidate_variant="full_faco",
                        baseline_row=baseline_bench,
                        candidate_row=candidate_bench,
                        prefix=prefix,
                        claim_use="performance guardrail for ablation",
                    )
                )
    return rows


def collect_db_bench_rows(root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    db_bench_re = re.compile(
        r"^(?P<benchmark>[A-Za-z0-9_]+)\s+:\s+"
        r"(?P<micros_per_op>[0-9.]+) micros/op "
        r"(?P<ops_per_sec>[0-9.]+) ops/sec "
        r"(?P<seconds>[0-9.]+) seconds "
        r"(?P<operations>[0-9]+) operations;\s+"
        r"(?P<mb_per_sec>[0-9.]+) MB/s"
    )
    for log_path in sorted(root.rglob("db_bench.log")):
        run_dir = log_path.parent
        experiment, variant, run = run_identity(run_dir, root)
        for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = db_bench_re.match(line)
            if not match:
                continue
            row = {
                "experiment": experiment,
                "variant": variant,
                "run": run,
                "benchmark": match.group("benchmark"),
                "micros_per_op": match.group("micros_per_op"),
                "ops_per_sec": match.group("ops_per_sec"),
                "seconds": match.group("seconds"),
                "operations": match.group("operations"),
                "mb_per_sec": match.group("mb_per_sec"),
                "result_dir": str(run_dir),
                "summary_scope": experiment,
                "source": "db_bench.log",
            }
            rows.append(row)
    if rows:
        return rows

    for summary in sorted(root.rglob("summary.csv")):
        rel = summary.parent.relative_to(root).as_posix()
        with summary.open(newline="", encoding="utf-8", errors="replace") as fh:
            for row in csv.DictReader(fh):
                row["summary_scope"] = rel
                row["source"] = "summary.csv"
                rows.append(row)
    return rows


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    out = ["| " + " | ".join(headers) + " |"]
    out.append("|" + "|".join("---" for _ in headers) + "|")
    for row in rows:
        out.append("| " + " | ".join(row) + " |")
    return out


def comparison_markdown_rows(rows: list[dict[str, str]]) -> list[list[str]]:
    return [
        [
            row.get("comparison_scope", ""),
            row.get("baseline_variant", ""),
            row.get("benchmark", ""),
            row.get("metric", ""),
            row.get("direction", ""),
            row.get("baseline_mean", ""),
            row.get("candidate_mean", ""),
            row.get("improvement_pct", ""),
            row.get("evidence", ""),
            row.get("status", ""),
        ]
        for row in rows
    ]


def find_comparison(
    rows: list[dict[str, str]],
    *,
    comparison_scope: str,
    baseline_variant: str,
    metric: str,
    benchmark: str = "",
) -> dict[str, str] | None:
    for row in rows:
        if (
            row.get("comparison_scope") == comparison_scope
            and row.get("baseline_variant") == baseline_variant
            and row.get("metric") == metric
            and row.get("benchmark", "") == benchmark
        ):
            return row
    return None


def claim_line(row: dict[str, str] | None, subject: str) -> str:
    if row is None or row.get("status") != "ok":
        return f"- {subject}: not comparable in this result."
    evidence = row.get("evidence", "")
    improvement = row.get("improvement_pct", "NA")
    if evidence == "improves":
        verdict = "supports an improvement claim"
    elif evidence == "regresses":
        verdict = "refutes an improvement claim"
    else:
        verdict = "does not show a meaningful change"
    return f"- {subject}: {verdict}; improvement_pct={improvement}."


def write_markdown(
    root: Path,
    out_md: Path,
    run_rows: list[dict[str, str]],
    bench_rows: list[dict[str, str]],
) -> None:
    missing_l2 = [
        row for row in run_rows
        if row.get("natural_aging_resets_total") == "NA"
        or row.get("lifetime_prediction_error_pct") == "NA"
    ]
    runtime_eval_rows = grouped_runtime_eval_rows(run_rows)
    bench_eval_rows = grouped_db_bench_eval_rows(bench_rows)
    baseline_comparison_rows = grouped_baseline_comparison_rows(
        runtime_eval_rows, bench_eval_rows
    )
    ablation_comparison_rows = grouped_ablation_comparison_rows(
        runtime_eval_rows, bench_eval_rows
    )

    lines = [
        "# M5 FACO summary",
        "",
        f"- result root: `{root}`",
        f"- run artifact rows: `{len(run_rows)}`",
        f"- db_bench summary rows: `{len(bench_rows)}`",
        f"- missing LACR L2 metric rows: `{len(missing_l2)}`",
        "",
        "## Evaluation Claim Boundary",
        "",
        "- Supported claim: FACO controls zone reset pressure and high-frag zones against matched FACO-internal baselines, while write amplification stays near 1 and throughput remains within the benchmark guardrail.",
        "- Unsupported claim: full_faco universally outperforms native or every ablation. Native has no FACO-local fragmentation counters, so missing FACO metrics must be treated as NA rather than zero.",
        "- Unsupported claim: LACR L2 lifetime prediction improves natural aging. The alpha-frozen M4 artifacts do not expose those counters yet.",
        "- Evidence mapping: Exp-1 is the performance guardrail, Exp-2 is fragmentation evolution, Exp-3 is LACR on/off movement behavior, and Exp-4 is the FACO-internal ablation table.",
        "",
        "## Runtime Evaluation",
        "",
        "Each row is grouped by experiment and variant. CI95 is the two-sided t half-width over run-level rows; n<2 reports NA.",
        "",
    ]
    lines += markdown_table(
        [
            "experiment",
            "variant",
            "runs",
            "WA mean",
            "WA std",
            "WA CI95",
            "resets mean",
            "resets std",
            "resets CI95",
            "high-frag mean",
            "high-frag std",
            "high-frag CI95",
            "reorg MB mean",
            "reorg MB std",
            "reorg MB CI95",
        ],
        [
            [
                row.get("experiment", ""),
                row.get("variant", ""),
                row.get("runs", ""),
                row.get("wa_mean", ""),
                row.get("wa_std", ""),
                row.get("wa_ci95", ""),
                row.get("zone_resets_mean", ""),
                row.get("zone_resets_std", ""),
                row.get("zone_resets_ci95", ""),
                row.get("high_frag_zones_mean", ""),
                row.get("high_frag_zones_std", ""),
                row.get("high_frag_zones_ci95", ""),
                row.get("reorg_mb_mean", ""),
                row.get("reorg_mb_std", ""),
                row.get("reorg_mb_ci95", ""),
            ]
            for row in runtime_eval_rows
        ],
    )

    if bench_eval_rows:
        lines += [
            "",
            "## db_bench Evaluation",
            "",
            "Throughput is grouped by experiment, variant, and benchmark. Do not merge full_faco across experiments.",
            "",
        ]
        lines += markdown_table(
            [
                "experiment",
                "variant",
                "benchmark",
                "rows",
                "ops/sec mean",
                "ops/sec std",
                "ops/sec CI95",
                "MB/s mean",
                "MB/s std",
                "MB/s CI95",
                "us/op mean",
            ],
            [
                [
                    row.get("experiment", ""),
                    variant,
                    benchmark,
                    row.get("rows", ""),
                    row.get("ops_per_sec_mean", ""),
                    row.get("ops_per_sec_std", ""),
                    row.get("ops_per_sec_ci95", ""),
                    row.get("mb_per_sec_mean", ""),
                    row.get("mb_per_sec_std", ""),
                    row.get("mb_per_sec_ci95", ""),
                    row.get("micros_per_op_mean", ""),
                ]
                for row in bench_eval_rows
                for variant in [row.get("variant", "")]
                for benchmark in [row.get("benchmark", "")]
            ],
        )

    baseline_summary_rows = [
        row
        for row in baseline_comparison_rows
        if row.get("comparison_scope") == "instrumented_fragmentation_baseline"
        or row.get("metric") in {"ops_per_sec", "micros_per_op"}
    ]
    ablation_runtime_rows = [
        row
        for row in ablation_comparison_rows
        if row.get("comparison_scope") == "ablation_runtime"
    ]

    lines += [
        "",
        "## Baseline Comparison",
        "",
        "`native` is used only for performance. `cfsm_only` is the matched fragmentation-observable baseline for FACO-local metrics.",
        "",
    ]
    lines += markdown_table(
        [
            "scope",
            "baseline",
            "benchmark",
            "metric",
            "direction",
            "baseline mean",
            "full_faco mean",
            "improvement %",
            "evidence",
            "status",
        ],
        comparison_markdown_rows(baseline_summary_rows),
    )

    lines += [
        "",
        "## Ablation Comparison",
        "",
        "These rows compare `full_faco` against each Exp-4 ablation. Positive improvement means the metric moves in the desired direction.",
        "",
    ]
    lines += markdown_table(
        [
            "scope",
            "baseline",
            "benchmark",
            "metric",
            "direction",
            "baseline mean",
            "full_faco mean",
            "improvement %",
            "evidence",
            "status",
        ],
        comparison_markdown_rows(ablation_runtime_rows),
    )

    lines += [
        "",
        "## What Can/Can Not Be Claimed",
        "",
        "- You can compare `full_faco` with `native` only for db_bench performance metrics in this protocol.",
        "- You can compare reset/high-frag pressure against `cfsm_only`, because it preserves FACO-local observability.",
        "- You can not claim native has zero high-frag zones; those counters are missing, so they stay `NA`.",
        claim_line(
            find_comparison(
                baseline_comparison_rows,
                comparison_scope="instrumented_fragmentation_baseline",
                baseline_variant="cfsm_only",
                metric="zone_reset_count",
            ),
            "`full_faco` versus `cfsm_only` on zone resets",
        ),
        claim_line(
            find_comparison(
                baseline_comparison_rows,
                comparison_scope="instrumented_fragmentation_baseline",
                baseline_variant="cfsm_only",
                metric="high_frag_zones",
            ),
            "`full_faco` versus `cfsm_only` on high-frag zones",
        ),
        claim_line(
            find_comparison(
                ablation_comparison_rows,
                comparison_scope="ablation_runtime",
                baseline_variant="without_lacr",
                metric="zone_reset_count",
            ),
            "`full_faco` versus `without_lacr` on zone resets",
        ),
        claim_line(
            find_comparison(
                ablation_comparison_rows,
                comparison_scope="ablation_runtime",
                baseline_variant="without_lacr",
                metric="high_frag_zones",
            ),
            "`full_faco` versus `without_lacr` on high-frag zones",
        ),
    ]

    lines += [
        "",
        "## Trace Coverage",
        "",
        "- `total_movement_bytes_proxy` is `reorg migrated bytes + LACR trace output bytes`; it is a protocol proxy until RocksDB compaction byte counters are joined from db_bench statistics.",
        "- `reorg_duplicate_movement_bytes_proxy` counts accepted reorg estimated-valid bytes when the reorg trace also marks the zone as compaction-touched.",
        "- `m5_runtime_eval.csv`, `m5_db_bench_eval.csv`, `m5_baseline_comparison.csv`, and `m5_ablation_comparison.csv` are the paper table inputs. The older raw CSV files are run-level evidence, not final table material.",
        "- `NA` for natural aging or lifetime prediction means the current alpha-frozen M4 artifacts do not expose LACR L2 data. Do not pretend those KPI are measured.",
        "",
    ]

    out_md.write_text("\n".join(lines), encoding="utf-8")


def write_protocol_json(out_md: Path, run_rows: list[dict[str, str]]) -> None:
    payload = {
        "schema": "faco-m5-summary-v1",
        "run_rows": len(run_rows),
        "evaluation_tables": [
            "m5_runtime_eval.csv",
            "m5_db_bench_eval.csv",
            "m5_baseline_comparison.csv",
            "m5_ablation_comparison.csv",
        ],
        "comparison_protocol": {
            "native": "performance baseline only",
            "cfsm_only": "fragmentation-observable baseline",
            "exp4": "primary ablation comparison",
        },
        "claim_boundary": {
            "supported": [
                "FACO controls zone reset pressure and high-frag zones against matched FACO-internal baselines.",
                "FACO keeps write amplification near 1 while throughput remains within the benchmark guardrail.",
            ],
            "unsupported": [
                "full_faco universally outperforms native or every ablation.",
                "LACR L2 lifetime prediction improves natural aging before M4 exposes those counters.",
            ],
        },
        "required_artifacts": [
            "faco_cfsm_summary.txt",
            "faco_cfsm_zones.csv",
            "faco_budget_trace.csv",
            "faco_reorg_trace.csv",
            "faco_lacr_trace.csv",
            "faco_runtime_metrics.txt",
            "faco_metrics.txt",
        ],
        "missing_l2_rows": sum(
            1
            for row in run_rows
            if row.get("natural_aging_resets_total") == "NA"
            or row.get("lifetime_prediction_error_pct") == "NA"
        ),
    }
    out_md.with_name("m5_protocol_manifest.json").write_text(
        json.dumps(payload, indent=2), encoding="utf-8"
    )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: summarize_traces.py RESULT_ROOT OUT_MD", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    out_md = Path(sys.argv[2])
    out_md.parent.mkdir(parents=True, exist_ok=True)

    run_rows = collect_run_rows(root)
    bench_rows = collect_db_bench_rows(root)
    runtime_eval_rows = grouped_runtime_eval_rows(run_rows)
    db_bench_eval_rows = grouped_db_bench_eval_rows(bench_rows)
    baseline_comparison_rows = grouped_baseline_comparison_rows(
        runtime_eval_rows, db_bench_eval_rows
    )
    ablation_comparison_rows = grouped_ablation_comparison_rows(
        runtime_eval_rows, db_bench_eval_rows
    )

    write_csv(out_md.with_name("m5_run_artifacts.csv"), run_rows)
    write_csv(out_md.with_name("m5_db_bench_rows.csv"), bench_rows)
    write_csv(out_md.with_name("m5_runtime_eval.csv"), runtime_eval_rows)
    write_csv(out_md.with_name("m5_db_bench_eval.csv"), db_bench_eval_rows)
    write_csv(out_md.with_name("m5_baseline_comparison.csv"), baseline_comparison_rows)
    write_csv(out_md.with_name("m5_ablation_comparison.csv"), ablation_comparison_rows)
    write_markdown(root, out_md, run_rows, bench_rows)
    write_protocol_json(out_md, run_rows)

    print(f"Wrote {out_md}")
    print(f"Wrote {out_md.with_name('m5_run_artifacts.csv')}")
    print(f"Wrote {out_md.with_name('m5_db_bench_rows.csv')}")
    print(f"Wrote {out_md.with_name('m5_runtime_eval.csv')}")
    print(f"Wrote {out_md.with_name('m5_db_bench_eval.csv')}")
    print(f"Wrote {out_md.with_name('m5_baseline_comparison.csv')}")
    print(f"Wrote {out_md.with_name('m5_ablation_comparison.csv')}")
    print(f"Wrote {out_md.with_name('m5_protocol_manifest.json')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
