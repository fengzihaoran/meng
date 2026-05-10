// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "plugin/zenfs/fs/faco_config.h"

#include <gtest/gtest.h>

#include <string>

#include "port/stack_trace.h"

namespace ROCKSDB_NAMESPACE {

TEST(FacoConfig, ParsesFixedM5Schema) {
  const std::string json = R"json(
{
  "frag_state": {
    "ema_alpha": 0.25,
    "tick_interval_us": 1000000
  },
  "budget_ctrl": {
    "B_min": 3,
    "B_max": 11,
    "Kp": 0.7,
    "Ki": 0.02,
    "P_target": 0.1,
    "theta_zvdr": 0.05
  },
  "reorg_planner": {
    "w1": 0.4, "w2": 0.3, "w3": 0.2, "w4": 0.1,
    "WA_factor": 1.0,
    "T_horizon_us": 60000000,
    "tau_trigger_init": 5242880
  },
  "lacr": {
    "enable_l1": true,
    "enable_l2": false,
    "pool_ratio_initial": [30, 40, 30],
    "default_lifetime_us_per_level": [
      300000000, 900000000, 1800000000,
      3600000000, 7200000000, 7200000000, 7200000000
    ],
    "w_synergy": 0.0,
    "w_waste": 1.0,
    "w_latency": 0.25
  }
}
)json";

  FacoConfig cfg = FacoConfig::ParseForTest(json, "test");

  ASSERT_TRUE(cfg.loaded());
  ASSERT_TRUE(cfg.frag_state.ema_alpha.has_value());
  ASSERT_FLOAT_EQ(*cfg.frag_state.ema_alpha, 0.25f);
  ASSERT_EQ(*cfg.budget_ctrl.B_min, 3);
  ASSERT_EQ(*cfg.budget_ctrl.B_max, 11);
  ASSERT_FLOAT_EQ(*cfg.budget_ctrl.Kp, 0.7f);
  ASSERT_EQ(*cfg.reorg_planner.T_horizon_us, 60000000ULL);
  ASSERT_FLOAT_EQ(*cfg.reorg_planner.tau_trigger_init, 5242880.0f);
  ASSERT_TRUE(*cfg.lacr.enable_l1);
  ASSERT_FALSE(*cfg.lacr.enable_l2);
  ASSERT_EQ((*cfg.lacr.pool_ratio_initial)[0], 30);
  ASSERT_EQ((*cfg.lacr.pool_ratio_initial)[2], 30);
  ASSERT_EQ((*cfg.lacr.default_lifetime_us_per_level)[6], 7200000000ULL);
  ASSERT_FLOAT_EQ(*cfg.lacr.w_waste, 1.0f);
}

TEST(FacoConfig, MissingFieldsRemainUnset) {
  FacoConfig cfg =
      FacoConfig::ParseForTest("{\"budget_ctrl\":{\"B_min\":4}}", "test");

  ASSERT_TRUE(cfg.loaded());
  ASSERT_EQ(*cfg.budget_ctrl.B_min, 4);
  ASSERT_FALSE(cfg.budget_ctrl.B_max.has_value());
  ASSERT_FALSE(cfg.reorg_planner.w1.has_value());
  ASSERT_FALSE(cfg.lacr.enable_l1.has_value());
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
