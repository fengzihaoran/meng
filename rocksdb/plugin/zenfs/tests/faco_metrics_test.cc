// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "plugin/zenfs/fs/faco_metrics.h"

#include <gtest/gtest.h>

#include <string>

#include "plugin/zenfs/fs/frag_state_table.h"
#include "port/stack_trace.h"

namespace ROCKSDB_NAMESPACE {

TEST(FacoMetrics, ExportsFragmentationAndRuntimeMetrics) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000,
                                /*num_zones=*/3);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/1000);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/500);
  table.OnAppend(/*zone_id=*/2, /*bytes=*/100);
  table.Tick(/*now_us=*/1000000);

  FacoMetricsSnapshot snapshot;
  snapshot.AddFragmentationMetrics(&table);
  snapshot.AddBudgetMetrics(
      "ZoneBudgetCtrl{budget=6, B_min=2, B_max=12, p_frag=0.5}");
  snapshot.AddReorgMetrics(
      "ReorgPlanner{tau_effective=123, executed_plans=2, "
      "migrated_bytes=4096, rejected_plans=3}");
  snapshot.AddLacrMetrics(
      "FacoLacrState{enabled=1, active_jobs=1, "
      "active_compaction_files=2, active_zones=1, recent_zones=1, "
      "trace_samples=4}");
  snapshot.AddRuntimeMetrics("zone_reset_count=7\nbytes_written=8192\n");

  const std::string text = snapshot.ToText();
  ASSERT_NE(text.find("faco.frag.zvdr.p50"), std::string::npos);
  ASSERT_NE(text.find("faco.frag.invalid_ratio.p99"), std::string::npos);
  ASSERT_NE(text.find("faco.budget.alloc_current"), std::string::npos);
  ASSERT_NE(text.find("faco.reorg.bytes_migrated_total"), std::string::npos);
  ASSERT_NE(text.find("faco.lacr.compaction_events_total"),
            std::string::npos);
  ASSERT_NE(text.find("faco.runtime.zone_reset_count"), std::string::npos);

  const std::string json = snapshot.ToJson();
  ASSERT_NE(json.find("\"metrics\""), std::string::npos);

  const std::string prom = snapshot.ToPrometheusText();
  ASSERT_NE(prom.find("faco_frag_zvdr_p50"), std::string::npos);
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
