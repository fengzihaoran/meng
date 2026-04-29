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

    // Simplified M3 contention model.  A non-zero value converts M2 budget
    // pressure into a byte-scale penalty; M4 can replace this with richer
    // compaction and queue-depth signals.
    float contention_penalty_bytes = 4.0f * 1024.0f * 1024.0f;
  };

  struct Plan {
    uint64_t victim_zone = 0;
    float net_benefit = 0.0f;
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

 private:
  struct TraceSample {
    uint64_t now_us = 0;
    uint64_t victim_zone = 0;
    float net_benefit = 0.0f;
    float tau_trigger = 0.0f;
    int accepted = 0;
    int current_budget = 0;
    int max_active_budget = 0;
  };

  static Config NormalizeConfig(Config cfg);
  float CurrentTauTriggerNoLock() const;
  float BudgetTauScale() const;
  int CurrentBudget() const;
  int MaxActiveBudget() const;
  void RecordTraceLocked(uint64_t now_us, const Plan& plan, bool accepted);

  Config cfg_;
  FragmentationStateTable* frag_;
  ZonedBlockDevice* zbd_;
  uint64_t zone_capacity_;

  mutable std::mutex mutex_;
  ExecuteFn execute_fn_;
  float tau_trigger_;
  float foreground_p99_ema_;
  uint64_t accepted_plans_;
  uint64_t executed_plans_;
  Plan last_plan_;
  std::vector<TraceSample> trace_;
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
