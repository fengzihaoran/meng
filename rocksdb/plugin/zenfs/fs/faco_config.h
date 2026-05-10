// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

/**
 * Fixed-schema FACO runtime configuration loaded from faco.json.
 *
 * The parser is intentionally small and dependency-free. It supports only the
 * numeric, boolean, and array fields listed in Task M5; malformed or missing
 * fields are ignored so existing M1-M4 environment-variable behavior remains
 * the fallback.
 */
class FacoConfig {
 public:
  struct FragState {
    std::optional<float> ema_alpha;
    std::optional<uint64_t> tick_interval_us;
  };

  struct BudgetCtrl {
    std::optional<int> B_min;
    std::optional<int> B_max;
    std::optional<float> Kp;
    std::optional<float> Ki;
    std::optional<float> P_target;
    std::optional<float> theta_zvdr;
    std::optional<uint64_t> update_interval_us;
  };

  struct ReorgPlannerCfg {
    std::optional<float> w1;
    std::optional<float> w2;
    std::optional<float> w3;
    std::optional<float> w4;
    std::optional<float> WA_factor;
    std::optional<uint64_t> T_horizon_us;
    std::optional<float> tau_trigger_init;
  };

  struct Lacr {
    std::optional<bool> enable_l1;
    std::optional<bool> enable_l2;
    std::optional<float> compaction_synergy_factor;
    std::optional<float> lifetime_ema_alpha;
    std::optional<uint64_t> lifetime_short_threshold_us;
    std::optional<uint64_t> lifetime_medium_threshold_us;
    std::optional<uint64_t> warmup_samples_per_level;
    std::optional<std::array<int, 3>> pool_ratio_initial;
    std::optional<std::array<uint64_t, 7>> default_lifetime_us_per_level;

    // Current alpha-frozen M4 knobs. These are read only when explicitly
    // present, so FACO M5 does not tune M4 by changing defaults.
    std::optional<float> w_synergy;
    std::optional<float> w_waste;
    std::optional<float> w_latency;
    std::optional<uint64_t> active_compaction_penalty_bytes;
    std::optional<uint64_t> recent_invalidation_bonus_bytes;
  };

  static FacoConfig LoadFromEnv();
  static FacoConfig LoadFromFile(const std::string& path);
  static FacoConfig ParseForTest(const std::string& text,
                                 const std::string& source);

  bool loaded() const { return loaded_; }
  const std::string& source() const { return source_; }
  std::string DebugString() const;

  FragState frag_state;
  BudgetCtrl budget_ctrl;
  ReorgPlannerCfg reorg_planner;
  Lacr lacr;

 private:
  bool loaded_ = false;
  std::string source_;
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
