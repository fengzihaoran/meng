// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rocksdb/io_status.h"
#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

class FragmentationStateTable;
class ZonedBlockDevice;

#ifndef FACO_ENABLE_REORG
#define FACO_ENABLE_REORG 1
#endif

/**
 * ReorgPlanner is FACO M3's EBCR reorganization policy layer.
 *
 * It does not rebuild CFSM.  Candidate zones come from M1's RBD-ranked top
 * victim list, and the trigger threshold is scaled by M2's active-zone budget
 * so a tighter foreground allocation budget allows more aggressive reorg.
 */
class ReorgPlanner {
 public:
  enum class TauMode {
    kFixed = 0,
    kAdaptive = 1,
  };

  struct Config {
    float w1 = 0.4f;
    float w2 = 0.3f;
    float w3 = 0.2f;
    float w4 = 0.1f;
    float WA_factor = 1.0f;
    uint64_t T_horizon_us = 60ULL * 1000ULL * 1000ULL;
    float tau_trigger_init = 5.0f * 1024.0f * 1024.0f;

    // M3 keeps RBD/top victims as the primary candidate source.  ZVDR remains
    // only the optional trend term already present in the Net(z) formula.
    size_t top_k = 8;
    float tau_min = 1.0f * 1024.0f * 1024.0f;
    float tau_max = 512.0f * 1024.0f * 1024.0f;
    float tau_ema_alpha = 0.2f;
    float foreground_p99_target_us = 1000.0f;
    float tau_pressure_gain = 0.25f;
    TauMode tau_mode = TauMode::kFixed;

    // Simplified M3 contention model.  A non-zero value converts M2 budget
    // pressure into a byte-scale penalty; M4 can replace this with richer
    // compaction and queue-depth signals.
    float contention_penalty_bytes = 4.0f * 1024.0f * 1024.0f;

    // Do not spend foreground bandwidth on zones whose useful live payload is
    // too small to justify a reorganization cycle.  A non-zero absolute byte
    // value overrides the zone-capacity ratio.
    uint64_t min_migrate_bytes = 0;
    float min_migrate_ratio = 0.03125f;

    // After a victim is executed, suppress it briefly so M3 does not keep
    // selecting the same top-RBD zone before CFSM state visibly changes.
    uint64_t victim_cooldown_us = 60ULL * 1000ULL * 1000ULL;

    // Adaptive tau admits only candidates above a rolling percentile of recent
    // eligible Net(z) values.  The budget term lowers the percentile under
    // tighter M2 active-zone budgets without blindly halving the threshold.
    size_t adaptive_history_size = 32;
    float adaptive_q_base = 0.85f;
    float adaptive_q_min = 0.70f;
    float adaptive_q_max = 0.90f;
    float adaptive_q_budget_gain = 0.15f;
    float accept_hysteresis = 0.01f;
    uint64_t adaptive_warmup_evals = 5;

    // Global M3 rate limiter.  This is separate from per-victim cooldown.
    uint64_t min_exec_interval_us = 0;
  };

  struct Plan {
    uint64_t victim_zone = 0;
    float net_benefit = 0.0f;
    uint64_t estimated_valid_bytes = 0;
  };

  using ExecuteFn = std::function<IOStatus(const Plan&)>;

  ReorgPlanner(const Config& cfg, FragmentationStateTable* frag,
               ZonedBlockDevice* zbd);

  /** Loads FACO_REORG_* environment variables and normalizes the result. */
  static Config LoadConfigFromEnv();

  /** Installs the ZenFS migration callback used by Execute(). */
  void SetExecuteFn(ExecuteFn fn);

  /** Returns the best currently actionable reorg plan, if Net(z) beats tau. */
  std::optional<Plan> NextPlan();

  /** Executes a plan through the configured ZenFS migration callback. */
  IOStatus Execute(const Plan& p);

  /** Records migration work completed by the ZenFS callback. */
  void RecordMigration(uint64_t extents, uint64_t bytes);

  /** Raises or lowers the base tau according to foreground read pressure. */
  void UpdateForegroundPressure(float p99_read_us);

  /** Returns the current effective tau after M2 budget scaling. */
  float CurrentTauTrigger() const;

  /** Returns a compact status string for logs and experiment artifacts. */
  std::string DebugString() const;

  /** Exports sampled M3 decisions as CSV for experiments/M3 scripts. */
  std::string ExportTraceCsv() const;

  virtual ~ReorgPlanner() = default;
 protected:
  // Virtual so M4 LACR can inject compaction synergy without editing M3.
  virtual float ComputeNet(uint64_t zone_id) const;

  // M3's contention penalty is intentionally simple and budget-derived.
  virtual float ContentionPenalty(uint64_t zone_id) const;

  // Test hook: production uses M2's current active-zone budget ratio.
  virtual float BudgetRatioForAdaptive() const;

 private:
  struct TraceSample {
    uint64_t now_us = 0;
    uint64_t victim_zone = 0;
    float net_benefit = 0.0f;
    float tau_trigger = 0.0f;
    std::string tau_mode;
    float adaptive_q = 0.0f;
    float adaptive_tau = 0.0f;
    int accepted = 0;
    int warmup = 0;
    int rate_limited = 0;
    int current_budget = 0;
    int max_active_budget = 0;
    size_t history_size = 0;
    uint64_t estimated_valid_bytes = 0;
    uint64_t cooldown_skipped = 0;
    uint64_t tiny_skipped = 0;
    std::string reason;
  };

  static Config NormalizeConfig(Config cfg);
  static const char* TauModeName(TauMode mode);
  uint64_t EffectiveMinMigrateBytes() const;
  float AdaptiveQuantileNoLock() const;
  float AdaptiveTauNoLock(float q) const;
  void RecordAdaptiveNetLocked(float net_benefit);
  float CurrentTauTriggerNoLock() const;
  float BudgetTauScale() const;
  int CurrentBudget() const;
  int MaxActiveBudget() const;
  void RecordTraceLocked(uint64_t now_us, const Plan& plan, bool accepted,
                         uint64_t cooldown_skipped, uint64_t tiny_skipped,
                         const std::string& reason, float tau_trigger,
                         float adaptive_q, float adaptive_tau, bool warmup,
                         bool rate_limited);

  Config cfg_;
  FragmentationStateTable* frag_;
  ZonedBlockDevice* zbd_;
  uint64_t zone_capacity_;

  mutable std::mutex mutex_;
  ExecuteFn execute_fn_;
  float tau_trigger_;
  float foreground_p99_ema_;
  uint64_t eval_count_;
  uint64_t no_candidate_count_;
  uint64_t rejected_plans_;
  uint64_t accepted_plans_;
  uint64_t executed_plans_;
  uint64_t migrated_extents_;
  uint64_t migrated_bytes_;
  uint64_t cooldown_skip_count_;
  uint64_t tiny_plan_skip_count_;
  uint64_t warmup_reject_count_;
  uint64_t rate_limited_reject_count_;
  float max_net_seen_;
  float last_adaptive_q_;
  float last_adaptive_tau_;
  uint64_t last_execution_us_;
  Plan last_plan_;
  std::unordered_map<uint64_t, uint64_t> zone_cooldown_until_us_;
  std::vector<float> adaptive_net_history_;
  std::vector<TraceSample> trace_;
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
