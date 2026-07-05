#!/usr/bin/env bash
# Validate the FACO M5 evaluation flow without accidentally launching device
# benchmarks. This script checks syntax, regenerates offline summaries, and
# verifies the comparison protocol expected by the paper tables.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

usage() {
  cat <<EOF
usage:
  bash experiments/M5/validate_eval_flow.sh offline [RESULT_ROOT] [OUT_DIR]
  bash experiments/M5/validate_eval_flow.sh check-result RESULT_ROOT
  bash experiments/M5/validate_eval_flow.sh check-dev-gates RESULT_ROOT
  bash experiments/M5/validate_eval_flow.sh print-dev-command
  bash experiments/M5/validate_eval_flow.sh print-paper-command

modes:
  offline          run bash syntax checks, regenerate summary from an existing
                   result root into OUT_DIR, then validate comparison tables
  check-result     validate comparison tables already written in RESULT_ROOT
  check-dev-gates  validate the stricter dev acceptance gates before paper
  print-dev-command
                   print the dev-scale validation command; does not run it
  print-paper-command
                   print the paper-scale command; does not run it
EOF
}

run_shell_syntax_checks() {
  local script
  for script in \
    run_exp_common.sh \
    run_exp1.sh \
    run_exp2.sh \
    run_exp3.sh \
    run_exp4.sh \
    run_all.sh; do
    bash -n "${SCRIPT_DIR}/${script}"
  done
}

default_result_root() {
  local fixed="${SCRIPT_DIR}/results/20260510-060423-run-all"
  local latest=""
  if [[ -d "${fixed}" ]]; then
    printf '%s\n' "${fixed}"
    return
  fi
  if [[ -d "${SCRIPT_DIR}/results" ]]; then
    latest="$(find "${SCRIPT_DIR}/results" -maxdepth 1 -type d -name '*-run-all' | sort | tail -n 1)"
  fi
  if [[ -n "${latest}" ]]; then
    printf '%s\n' "${latest}"
    return
  fi
  printf '%s\n' "${fixed}"
}

run_offline_summary() {
  local result_root="$1"
  local out_dir="$2"
  mkdir -p "${out_dir}"
  python3 "${SCRIPT_DIR}/summarize_traces.py" \
    "${result_root}" \
    "${out_dir}/m5_summary.md"
}

assert_eval_outputs() {
  local summary_dir="$1"
  python3 - "${summary_dir}" <<'PY'
import csv
import sys
from pathlib import Path

root = Path(sys.argv[1])
required = [
    "m5_summary.md",
    "m5_runtime_eval.csv",
    "m5_db_bench_eval.csv",
    "m5_high_frag_diagnostics.csv",
    "m5_high_frag_summary.csv",
    "m5_reorg_candidate_rows.csv",
    "m5_reorg_candidate_summary.csv",
    "m5_baseline_comparison.csv",
    "m5_ablation_comparison.csv",
    "m5_protocol_manifest.json",
]
missing = [name for name in required if not (root / name).exists()]
if missing:
    raise SystemExit(f"missing required summary artifacts: {missing}")


def read_rows(name):
    with (root / name).open(newline="", encoding="utf-8", errors="replace") as fh:
        return list(csv.DictReader(fh))


def find_row(rows, **wanted):
    for row in rows:
        if all(row.get(key) == value for key, value in wanted.items()):
            return row
    return None


baseline = read_rows("m5_baseline_comparison.csv")
ablation = read_rows("m5_ablation_comparison.csv")
high_frag = read_rows("m5_high_frag_diagnostics.csv")
high_frag_summary = read_rows("m5_high_frag_summary.csv")
runtime = read_rows("m5_runtime_eval.csv")
db_bench = read_rows("m5_db_bench_eval.csv")

if not baseline:
    raise SystemExit("m5_baseline_comparison.csv is empty")
if not ablation:
    raise SystemExit("m5_ablation_comparison.csv is empty")
if not high_frag:
    raise SystemExit("m5_high_frag_diagnostics.csv is empty")
if not high_frag_summary:
    raise SystemExit("m5_high_frag_summary.csv is empty")

for metric in ["zone_reset_count", "high_frag_zones"]:
    row = find_row(
        ablation,
        comparison_scope="ablation_runtime",
        baseline_variant="native",
        metric=metric,
    )
    if row is None:
        raise SystemExit(f"missing native runtime row for {metric}")
    if row.get("status") != "not_comparable":
        raise SystemExit(
            f"native {metric} must be not_comparable, got {row.get('status')}"
        )

for metric in ["zone_reset_count", "high_frag_zones", "write_amplification"]:
    row = find_row(
        baseline,
        comparison_scope="instrumented_fragmentation_baseline",
        baseline_variant="cfsm_only",
        metric=metric,
    )
    if row is None:
        raise SystemExit(f"missing cfsm_only baseline comparison for {metric}")
    if row.get("status") != "ok":
        raise SystemExit(
            f"cfsm_only {metric} comparison must be ok, got {row.get('status')}"
        )

for metric in ["zone_reset_count", "high_frag_zones"]:
    row = find_row(
        ablation,
        comparison_scope="ablation_runtime",
        baseline_variant="without_lacr",
        metric=metric,
    )
    if row is None:
        raise SystemExit(f"missing without_lacr ablation row for {metric}")

for variant in ["cfsm_only", "full_faco", "without_ebcr", "without_lacr"]:
    row = find_row(
        high_frag_summary,
        experiment="exp4",
        variant=variant,
    )
    if row is None:
        raise SystemExit(f"missing exp4 high-frag diagnostic summary for {variant}")
    if row.get("high_frag_zones_n") == "0":
        raise SystemExit(f"exp4 {variant} high-frag diagnostic has no numeric rows")

if any(row.get("variant") == "full_faco" and not row.get("experiment") for row in runtime):
    raise SystemExit("full_faco runtime rows must retain experiment identity")

if any(row.get("variant") == "full_faco" and not row.get("experiment") for row in db_bench):
    raise SystemExit("full_faco db_bench rows must retain experiment identity")

print(f"validated M5 evaluation artifacts in {root}")
PY
}

assert_dev_gates() {
  local summary_dir="$1"
  python3 - "${summary_dir}" \
    "${M5_GATE_MIN_EVALS:-2}" \
    "${M5_GATE_MIN_WA_MAX:-1.01}" \
    "${M5_GATE_MIN_FINAL_COVERAGE_PCT:-10}" \
    "${M5_GATE_MIN_HIGH_FRAG_SELECTED_PCT:-99}" \
    "${M5_GATE_MIN_HIGH_FRAG_ACCEPTED_PCT:-99}" <<'PY'
import csv
import sys
from pathlib import Path

root = Path(sys.argv[1])
min_evals = float(sys.argv[2])
max_wa = float(sys.argv[3])
min_coverage_pct = float(sys.argv[4])
min_selected_pct = float(sys.argv[5])
min_accepted_pct = float(sys.argv[6])


def read_rows(name):
    with (root / name).open(newline="", encoding="utf-8", errors="replace") as fh:
        return list(csv.DictReader(fh))


def find_row(rows, **wanted):
    for row in rows:
        if all(row.get(key) == value for key, value in wanted.items()):
            return row
    return None


def number(row, key):
    if row is None:
        raise SystemExit(f"missing row for {key}")
    value = row.get(key, "")
    if value in ("", "NA"):
        raise SystemExit(f"{key} is not numeric: {value}")
    return float(value)


baseline = read_rows("m5_baseline_comparison.csv")
runtime = read_rows("m5_runtime_eval.csv")
high_frag_summary = read_rows("m5_high_frag_summary.csv")
candidate_summary = read_rows("m5_reorg_candidate_summary.csv")

for metric in ["zone_reset_count", "high_frag_zones"]:
    row = find_row(
        baseline,
        comparison_scope="instrumented_fragmentation_baseline",
        experiment="exp4",
        baseline_variant="cfsm_only",
        metric=metric,
    )
    if row is None:
        raise SystemExit(f"missing exp4 cfsm_only comparison for {metric}")
    if row.get("evidence") != "improves":
        raise SystemExit(
            f"full_faco vs cfsm_only must improve {metric}, got {row.get('evidence')} "
            f"with improvement_pct={row.get('improvement_pct')}"
        )

full_runtime = find_row(runtime, experiment="exp4", variant="full_faco")
wa = number(full_runtime, "wa_mean")
if wa >= max_wa:
    raise SystemExit(f"full_faco WA must be < {max_wa}, got {wa}")

full_candidates = find_row(
    candidate_summary, experiment="exp4", variant="full_faco"
)
evals = number(full_candidates, "candidate_evals_mean")
accepted = number(full_candidates, "accepted_rows_mean")
selected_pct = number(full_candidates, "high_frag_selected_pct_mean")
accepted_pct = number(full_candidates, "high_frag_accepted_pct_mean")
if evals < min_evals:
    raise SystemExit(f"full_faco candidate_evals_mean must be >= {min_evals}, got {evals}")
if accepted < min_evals:
    raise SystemExit(f"full_faco accepted_rows_mean must be >= {min_evals}, got {accepted}")
if selected_pct < min_selected_pct:
    raise SystemExit(
        f"full_faco high_frag_selected_pct_mean must be >= {min_selected_pct}, got {selected_pct}"
    )
if accepted_pct < min_accepted_pct:
    raise SystemExit(
        f"full_faco high_frag_accepted_pct_mean must be >= {min_accepted_pct}, got {accepted_pct}"
    )

full_high_frag = find_row(high_frag_summary, experiment="exp4", variant="full_faco")
coverage = number(full_high_frag, "accepted_final_high_frag_coverage_pct_mean")
if coverage < min_coverage_pct:
    raise SystemExit(
        f"full_faco accepted_final_high_frag_coverage_pct_mean must be >= "
        f"{min_coverage_pct}, got {coverage}"
    )

print(
    "validated M5 dev gates: "
    f"evals={evals}, accepted={accepted}, WA={wa}, "
    f"final_coverage_pct={coverage}, selected_pct={selected_pct}, "
    f"accepted_pct={accepted_pct}"
)
PY
}

print_dev_command() {
  cat <<'EOF'
cd ~/rocksdb

CONFIRM_DEVICE_BENCH=1 \
ZBD=nvme0n1 \
M5_PROFILE=dev \
M5_RUNS=3 \
FACO_GC_INTERVAL_US=5000000 \
M5_EXP4_BENCHMARKS=fillrandom,overwrite,overwrite,overwrite \
bash experiments/M5/run_all.sh
EOF
}

print_paper_command() {
  cat <<'EOF'
cd ~/rocksdb

CONFIRM_DEVICE_BENCH=1 \
ZBD=nvme0n1 \
M5_PROFILE=paper \
M5_RUNS=5 \
FACO_GC_INTERVAL_US=5000000 \
M5_EXP4_BENCHMARKS=fillrandom,overwrite,overwrite,overwrite \
bash experiments/M5/run_all.sh
EOF
}

mode="${1:-}"
case "${mode}" in
  offline)
    result_root="${2:-$(default_result_root)}"
    out_dir="${3:-/tmp/faco_m5_eval_flow_check}"
    run_shell_syntax_checks
    run_offline_summary "${result_root}" "${out_dir}"
    assert_eval_outputs "${out_dir}"
    ;;
  check-result)
    if [[ $# -ne 2 ]]; then
      usage >&2
      exit 2
    fi
    assert_eval_outputs "$2"
    ;;
  check-dev-gates)
    if [[ $# -ne 2 ]]; then
      usage >&2
      exit 2
    fi
    assert_eval_outputs "$2"
    assert_dev_gates "$2"
    ;;
  print-dev-command)
    print_dev_command
    ;;
  print-paper-command)
    print_paper_command
    ;;
  -h|--help|help|"")
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
