// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

class FragmentationStateTable;

#ifndef FACO_ENABLE_BUDGET
#define FACO_ENABLE_BUDGET 1
#endif

/**
 * ZoneBudgetCtrl is FACO M2's elastic active-zone budget controller.
 *
 * The allocation hot path only calls GetCurrentAllocBudget(), which is a single
 * atomic load.  Periodic Update() calls read CFSM's existing RBD/top-victim
 * signals and move the budget with a bounded PI controller.
 */
class ZoneBudgetCtrl {
 public:
  struct Config {
    int B_min = 2;
    int B_max = 12;
    float Kp = 0.6f;
    float Ki = 0.05f;
    float P_target = 0.0f;
    float theta_zvdr = 0.05f;
    uint64_t update_interval_us = 1000000;

    // M2 uses RBD/top victim zones as the primary pressure signal.  ZVDR can be
    // enabled as a weak trend term, but defaults to off for current M1 output.
    size_t top_k = 8;
    float rbd_threshold = 0.05f;
    float rbd_weight = 1.0f;
    float zvdr_weight = 0.0f;
    float integral_min = -16.0f;
    float integral_max = 16.0f;
  };

  explicit ZoneBudgetCtrl(const Config& cfg, FragmentationStateTable* frag);

  /** Returns the current active-zone allocation budget for AllocateIOZone. */
  int GetCurrentAllocBudget() const;

  /** Returns the normalized upper bound of M2's allocation budget. */
  int GetMaxAllocBudget() const;

  /** Recomputes fragmentation pressure and adjusts the PI budget. */
  void Update(uint64_t now_us);

  /** Returns the latest normalized fragmentation pressure. */
  float CurrentPFrag() const;

  /** Returns a compact status string for logs and experiment artifacts. */
  std::string DebugString() const;

  /** Exports the sampled budget curve as CSV for experiments/M2 scripts. */
  std::string ExportTraceCsv() const;

 private:
  struct PressureStats {
    float p_frag = 0.0f;
    float avg_rbd = 0.0f;
    float max_rbd = 0.0f;
    float trend_pressure = 0.0f;
    size_t victim_count = 0;
  };

  struct TraceSample {
    uint64_t now_us = 0;
    int budget = 0;
    float p_frag = 0.0f;
    float avg_rbd = 0.0f;
    float max_rbd = 0.0f;
    float trend_pressure = 0.0f;
    size_t victim_count = 0;
  };

  static Config NormalizeConfig(Config cfg);
  PressureStats ComputePressure() const;
  void RecordTraceLocked(uint64_t now_us, const PressureStats& stats,
                         int budget);

  Config cfg_;
  FragmentationStateTable* frag_;
  std::atomic<int> current_budget_;

  mutable std::mutex mutex_;
  float budget_float_;
  float integral_;
  float current_p_frag_;
  float current_avg_rbd_;
  float current_max_rbd_;
  float current_trend_pressure_;
  size_t current_victim_count_;
  uint64_t last_update_us_;
  std::vector<TraceSample> trace_;
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
