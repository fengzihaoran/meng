// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include <string>
#include <vector>

#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

class FragmentationStateTable;

enum class FacoMetricKind {
  kGauge = 0,
  kCounter = 1,
  kHistogram = 2,
};

struct FacoMetricSample {
  std::string name;
  FacoMetricKind kind = FacoMetricKind::kGauge;
  double value = 0.0;
};

/**
 * FACO M5 metric snapshot.
 *
 * This is a ZenFS-local export surface. It mirrors RocksDB Statistics naming
 * style but does not add RocksDB core ticker enums, which would violate the
 * FACO project boundary that only plugin/zenfs may be changed.
 */
class FacoMetricsSnapshot {
 public:
  void AddGauge(const std::string& name, double value);
  void AddCounter(const std::string& name, double value);
  void AddHistogram(const std::string& name, double value);

  /** Adds CFSM percentile and max metrics from the current zone CSV. */
  void AddFragmentationMetrics(const FragmentationStateTable* frag);

  /** Adds selected M2 metrics from ZoneBudgetCtrl::DebugString(). */
  void AddBudgetMetrics(const std::string& debug);

  /** Adds selected M3/M4 metrics from ReorgPlanner::DebugString(). */
  void AddReorgMetrics(const std::string& debug);

  /** Adds selected LACR event-state metrics from FacoLacrState::DebugString(). */
  void AddLacrMetrics(const std::string& debug);

  /** Adds ZenFS runtime key=value counters. */
  void AddRuntimeMetrics(const std::string& kv_text);

  std::string ToText() const;
  std::string ToJson() const;
  std::string ToPrometheusText() const;

 private:
  std::vector<FacoMetricSample> samples_;
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
