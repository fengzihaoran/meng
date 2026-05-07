// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "plugin/zenfs/fs/zone_budget_ctrl.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "plugin/zenfs/fs/frag_state_table.h"
#include "port/stack_trace.h"

namespace ROCKSDB_NAMESPACE {

namespace {

ZoneBudgetCtrl::Config TestConfig() {
  ZoneBudgetCtrl::Config cfg;
  cfg.B_min = 2;
  cfg.B_max = 12;
  cfg.Kp = 2.0f;
  cfg.Ki = 0.0f;
  cfg.P_target = 0.0f;
  cfg.update_interval_us = 0;
  cfg.top_k = 4;
  cfg.rbd_threshold = 0.0f;
  cfg.zvdr_weight = 0.0f;
  return cfg;
}

void SeedSparseVictims(FragmentationStateTable* table) {
  table->OnAppend(/*zone_id=*/0, /*bytes=*/100);
  table->OnAppend(/*zone_id=*/1, /*bytes=*/150);
  table->OnAppend(/*zone_id=*/2, /*bytes=*/300);
  table->OnAppend(/*zone_id=*/3, /*bytes=*/800);
}

}  // namespace

TEST(BudgetCtrl, InitialBudgetEqualsBMax) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/4);
  ZoneBudgetCtrl::Config cfg = TestConfig();
  cfg.B_max = 9;

  ZoneBudgetCtrl ctrl(cfg, &table);

  ASSERT_EQ(ctrl.GetCurrentAllocBudget(), 9);
  ASSERT_EQ(ctrl.GetMaxAllocBudget(), 9);
  ASSERT_EQ(ctrl.CurrentPFrag(), 0.0f);
}

TEST(BudgetCtrl, BudgetShrinksWhenPFragHigh) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/4);
  SeedSparseVictims(&table);
  ZoneBudgetCtrl ctrl(TestConfig(), &table);

  ctrl.Update(/*now_us=*/1000);

  ASSERT_LT(ctrl.GetCurrentAllocBudget(), 12);
  ASSERT_GT(ctrl.CurrentPFrag(), 0.0f);
}

TEST(BudgetCtrl, BudgetExpandsWhenPFragLow) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/4);
  SeedSparseVictims(&table);
  ZoneBudgetCtrl ctrl(TestConfig(), &table);

  for (int i = 0; i < 4; ++i) {
    ctrl.Update(/*now_us=*/1000 + i);
  }
  const int shrunk_budget = ctrl.GetCurrentAllocBudget();
  ASSERT_LT(shrunk_budget, 12);

  for (uint64_t zone = 0; zone < 4; ++zone) {
    table.OnZoneReset(zone);
  }
  ctrl.Update(/*now_us=*/2000);

  ASSERT_GT(ctrl.GetCurrentAllocBudget(), shrunk_budget);
  ASSERT_EQ(ctrl.CurrentPFrag(), 0.0f);
}

TEST(BudgetCtrl, RespectsBMinBMax) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/4);
  SeedSparseVictims(&table);
  ZoneBudgetCtrl::Config cfg = TestConfig();
  cfg.B_min = 3;
  cfg.B_max = 7;
  cfg.Kp = 8.0f;
  ZoneBudgetCtrl ctrl(cfg, &table);

  for (int i = 0; i < 20; ++i) {
    ctrl.Update(/*now_us=*/1000 + i);
  }
  ASSERT_EQ(ctrl.GetCurrentAllocBudget(), 3);

  for (uint64_t zone = 0; zone < 4; ++zone) {
    table.OnZoneReset(zone);
  }
  for (int i = 0; i < 20; ++i) {
    ctrl.Update(/*now_us=*/2000 + i);
  }
  ASSERT_EQ(ctrl.GetCurrentAllocBudget(), 7);
}

TEST(BudgetCtrl, PIControllerStable) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/4);
  SeedSparseVictims(&table);
  ZoneBudgetCtrl::Config cfg = TestConfig();
  cfg.Kp = 1.0f;
  cfg.Ki = 0.1f;
  ZoneBudgetCtrl ctrl(cfg, &table);

  int prev = ctrl.GetCurrentAllocBudget();
  for (int i = 0; i < 16; ++i) {
    ctrl.Update(/*now_us=*/1000 + i);
    const int current = ctrl.GetCurrentAllocBudget();
    ASSERT_LE(current, prev);
    prev = current;
  }

  for (uint64_t zone = 0; zone < 4; ++zone) {
    table.OnZoneReset(zone);
  }

  prev = ctrl.GetCurrentAllocBudget();
  for (int i = 0; i < 16; ++i) {
    ctrl.Update(/*now_us=*/2000 + i);
    const int current = ctrl.GetCurrentAllocBudget();
    ASSERT_GE(current, prev);
    prev = current;
  }
}

TEST(BudgetCtrl, IntegralAntiWindup) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/4);
  SeedSparseVictims(&table);
  ZoneBudgetCtrl::Config cfg = TestConfig();
  cfg.Kp = 1.0f;
  cfg.Ki = 0.5f;
  cfg.integral_min = -2.0f;
  cfg.integral_max = 2.0f;
  ZoneBudgetCtrl ctrl(cfg, &table);

  for (int i = 0; i < 80; ++i) {
    ctrl.Update(/*now_us=*/1000 + i);
  }
  ASSERT_EQ(ctrl.GetCurrentAllocBudget(), cfg.B_min);

  for (uint64_t zone = 0; zone < 4; ++zone) {
    table.OnZoneReset(zone);
  }
  for (int i = 0; i < 20; ++i) {
    ctrl.Update(/*now_us=*/2000 + i);
  }

  ASSERT_EQ(ctrl.GetCurrentAllocBudget(), cfg.B_max);
}

TEST(BudgetCtrl, UsesRBDTopVictimsWithoutZVDR) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/5);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/50);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/100);
  table.OnAppend(/*zone_id=*/2, /*bytes=*/900);
  ZoneBudgetCtrl::Config cfg = TestConfig();
  cfg.top_k = 2;
  cfg.zvdr_weight = 0.0f;
  ZoneBudgetCtrl ctrl(cfg, &table);

  ctrl.Update(/*now_us=*/1000);

  ASSERT_GT(ctrl.CurrentPFrag(), 0.0f);
  const std::string trace = ctrl.ExportTraceCsv();
  ASSERT_NE(trace.find("victim_count"), std::string::npos);
  ASSERT_NE(trace.find(",2\n"), std::string::npos);
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
