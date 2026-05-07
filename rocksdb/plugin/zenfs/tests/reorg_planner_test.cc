// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "plugin/zenfs/fs/reorg_planner.h"

#include <gtest/gtest.h>

#if FACO_ENABLE_LACR
#include <cstdlib>
#endif

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "db/faco_lacr_state.h"
#include "plugin/zenfs/fs/frag_state_table.h"
#include "port/stack_trace.h"

namespace ROCKSDB_NAMESPACE {

namespace {

ReorgPlanner::Config TestConfig() {
  ReorgPlanner::Config cfg;
  cfg.tau_trigger_init = 0.0f;
  cfg.tau_min = 0.0f;
  cfg.tau_max = 1024.0f * 1024.0f * 1024.0f;
  cfg.top_k = 4;
  cfg.contention_penalty_bytes = 0.0f;
  cfg.min_migrate_bytes = 0;
  cfg.min_migrate_ratio = 0.0f;
  cfg.victim_cooldown_us = 0;
  cfg.min_exec_interval_us = 0;
  return cfg;
}

#if FACO_ENABLE_LACR

class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    const char* old = std::getenv(name);
    if (old != nullptr) {
      had_old_ = true;
      old_value_ = old;
    }
    setenv(name, value, /*overwrite=*/1);
  }

  ~ScopedEnv() {
    if (had_old_) {
      setenv(name_.c_str(), old_value_.c_str(), /*overwrite=*/1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  bool had_old_ = false;
  std::string old_value_;
};

#endif  // FACO_ENABLE_LACR

void SeedReorgCandidates(FragmentationStateTable* table) {
  table->OnAppend(/*zone_id=*/0, /*bytes=*/100);
  table->OnAppend(/*zone_id=*/1, /*bytes=*/500);
  table->OnAppend(/*zone_id=*/2, /*bytes=*/800);
}

class OverridePlanner : public ReorgPlanner {
 public:
  using ReorgPlanner::ReorgPlanner;

 protected:
  float ComputeNet(uint64_t zone_id) const override {
    return zone_id == 2 ? 1000000.0f : 1.0f;
  }
};

#if FACO_ENABLE_LACR

class ExposedPlanner : public ReorgPlanner {
 public:
  using ReorgPlanner::ReorgPlanner;

  float NetForTest(uint64_t zone_id) const { return ComputeNet(zone_id); }
};

CompactionJobInfo MakeLacrCompaction(int job_id, const std::string& file,
                                     uint64_t input_bytes,
                                     uint64_t output_bytes) {
  CompactionJobInfo info;
  info.job_id = job_id;
  info.base_input_level = 1;
  info.output_level = 2;
  info.input_files.push_back(file);
  info.output_files.push_back("/000099.sst");
  info.stats.total_input_bytes = input_bytes;
  info.stats.total_output_bytes = output_bytes;
  return info;
}

#endif  // FACO_ENABLE_LACR

class CooldownPlanner : public ReorgPlanner {
 public:
  using ReorgPlanner::ReorgPlanner;

 protected:
  float ComputeNet(uint64_t zone_id) const override {
    return zone_id == 0 ? 1000.0f : 900.0f;
  }
};

class SequencePlanner : public ReorgPlanner {
 public:
  SequencePlanner(const Config& cfg, FragmentationStateTable* frag,
                  ZonedBlockDevice* zbd, std::vector<float> net_values,
                  float budget_ratio = 1.0f)
      : ReorgPlanner(cfg, frag, zbd),
        net_values_(std::move(net_values)),
        budget_ratio_(budget_ratio) {}

 protected:
  float ComputeNet(uint64_t zone_id) const override {
    (void)zone_id;
    if (net_values_.empty()) return 0.0f;
    const size_t index = std::min(call_index_, net_values_.size() - 1);
    call_index_++;
    return net_values_[index];
  }

  float BudgetRatioForAdaptive() const override { return budget_ratio_; }

 private:
  std::vector<float> net_values_;
  float budget_ratio_;
  mutable size_t call_index_ = 0;
};

}  // namespace

TEST(ReorgPlanner, NoReorgWhenAllZonesValid) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/1000);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/1000);
  ReorgPlanner planner(TestConfig(), &table, /*zbd=*/nullptr);

  ASSERT_FALSE(planner.NextPlan().has_value());
}

TEST(ReorgPlanner, PicksHighestNetBenefit) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/3);
  SeedReorgCandidates(&table);
  ReorgPlanner planner(TestConfig(), &table, /*zbd=*/nullptr);

  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();

  ASSERT_TRUE(plan.has_value());
  ASSERT_EQ(plan->victim_zone, 0);
  ASSERT_GT(plan->net_benefit, 0.0f);
}

TEST(ReorgPlanner, RespectsTauTrigger) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/3);
  SeedReorgCandidates(&table);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.tau_trigger_init = 1000000.0f;
  ReorgPlanner planner(cfg, &table, /*zbd=*/nullptr);

  ASSERT_FALSE(planner.NextPlan().has_value());
}

TEST(ReorgPlanner, RejectsTinyVictimsBelowMigrateThreshold) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/200);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.min_migrate_bytes = 300;
  ReorgPlanner planner(cfg, &table, /*zbd=*/nullptr);

  ASSERT_FALSE(planner.NextPlan().has_value());
  ASSERT_NE(planner.DebugString().find("tiny_plan_skip_count=2"),
            std::string::npos);
}

TEST(ReorgPlanner, SkipsTinyVictimAndUsesNextActionableZone) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/500);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.min_migrate_bytes = 300;
  ReorgPlanner planner(cfg, &table, /*zbd=*/nullptr);

  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();

  ASSERT_TRUE(plan.has_value());
  ASSERT_EQ(plan->victim_zone, 1);
  ASSERT_EQ(plan->estimated_valid_bytes, 500);
}

TEST(ReorgPlanner, CooldownSuppressesRecentlyExecutedVictim) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/100);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.victim_cooldown_us = 60ULL * 1000ULL * 1000ULL;
  CooldownPlanner planner(cfg, &table, /*zbd=*/nullptr);
  planner.SetExecuteFn([](const ReorgPlanner::Plan&) {
    return IOStatus::OK();
  });

  std::optional<ReorgPlanner::Plan> first = planner.NextPlan();
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->victim_zone, 0);
  ASSERT_TRUE(planner.Execute(*first).ok());

  std::optional<ReorgPlanner::Plan> second = planner.NextPlan();
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(second->victim_zone, 1);
  ASSERT_NE(planner.DebugString().find("cooldown_skip_count=1"),
            std::string::npos);
}

TEST(ReorgPlanner, AdaptiveWarmupRejectsAndRecordsHistory) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.tau_mode = ReorgPlanner::TauMode::kAdaptive;
  cfg.adaptive_history_size = 8;
  cfg.adaptive_warmup_evals = 2;
  cfg.adaptive_q_base = 0.5f;
  cfg.adaptive_q_min = 0.5f;
  cfg.adaptive_q_max = 0.5f;
  cfg.adaptive_q_budget_gain = 0.0f;
  cfg.accept_hysteresis = 0.0f;
  SequencePlanner planner(cfg, &table, /*zbd=*/nullptr, {10.0f, 20.0f});

  ASSERT_FALSE(planner.NextPlan().has_value());
  ASSERT_FALSE(planner.NextPlan().has_value());

  const std::string debug = planner.DebugString();
  ASSERT_NE(debug.find("warmup_reject_count=2"), std::string::npos);
  ASSERT_NE(debug.find("adaptive_history_size=2/8"), std::string::npos);
}

TEST(ReorgPlanner, AdaptivePercentileAcceptsOnlyHighNetCandidate) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.tau_mode = ReorgPlanner::TauMode::kAdaptive;
  cfg.adaptive_history_size = 8;
  cfg.adaptive_warmup_evals = 0;
  cfg.adaptive_q_base = 0.5f;
  cfg.adaptive_q_min = 0.5f;
  cfg.adaptive_q_max = 0.5f;
  cfg.adaptive_q_budget_gain = 0.0f;
  cfg.accept_hysteresis = 0.0f;
  cfg.adaptive_warmup_evals = 2;
  SequencePlanner planner(cfg, &table, /*zbd=*/nullptr,
                          {10.0f, 10.0f, 30.0f});

  ASSERT_FALSE(planner.NextPlan().has_value());
  ASSERT_FALSE(planner.NextPlan().has_value());
  ASSERT_TRUE(planner.NextPlan().has_value());
}

TEST(ReorgPlanner, AdaptiveBudgetPressureLowersQuantileOnlyWithinBounds) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.tau_mode = ReorgPlanner::TauMode::kAdaptive;
  cfg.adaptive_history_size = 8;
  cfg.adaptive_warmup_evals = 1;
  cfg.adaptive_q_base = 0.85f;
  cfg.adaptive_q_min = 0.70f;
  cfg.adaptive_q_max = 0.90f;
  cfg.adaptive_q_budget_gain = 0.15f;

  SequencePlanner high_budget(cfg, &table, /*zbd=*/nullptr, {100.0f}, 1.0f);
  ASSERT_FALSE(high_budget.NextPlan().has_value());
  ASSERT_NE(high_budget.DebugString().find("adaptive_q_last=0.85"),
            std::string::npos);

  SequencePlanner low_budget(cfg, &table, /*zbd=*/nullptr, {100.0f}, 0.0f);
  ASSERT_FALSE(low_budget.NextPlan().has_value());
  ASSERT_NE(low_budget.DebugString().find("adaptive_q_last=0.7"),
            std::string::npos);
}

TEST(ReorgPlanner, AdaptiveBudgetRatioUsesM2BudgetRange) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.tau_mode = ReorgPlanner::TauMode::kAdaptive;
  cfg.adaptive_history_size = 8;
  cfg.adaptive_warmup_evals = 1;
  cfg.adaptive_q_base = 0.85f;
  cfg.adaptive_q_min = 0.70f;
  cfg.adaptive_q_max = 0.90f;
  cfg.adaptive_q_budget_gain = 0.15f;

  SequencePlanner m2_min_budget(cfg, &table, /*zbd=*/nullptr, {100.0f}, 0.5f);

  ASSERT_FALSE(m2_min_budget.NextPlan().has_value());
  ASSERT_NE(m2_min_budget.DebugString().find("adaptive_q_last=0.775"),
            std::string::npos);
}

TEST(ReorgPlanner, RateLimiterPreventsBackToBackExecutions) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.min_exec_interval_us = 60ULL * 1000ULL * 1000ULL;
  ReorgPlanner planner(cfg, &table, /*zbd=*/nullptr);
  planner.SetExecuteFn([](const ReorgPlanner::Plan&) {
    return IOStatus::OK();
  });

  std::optional<ReorgPlanner::Plan> first = planner.NextPlan();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(planner.Execute(*first).ok());
  ASSERT_FALSE(planner.NextPlan().has_value());
  ASSERT_NE(planner.DebugString().find("rate_limited_reject_count=1"),
            std::string::npos);
}

TEST(ReorgPlanner, MinMigrateRatioDerivesEightMiBForTwoHundredFiftySixMiBZone) {
  constexpr uint64_t kZoneSize = 256ULL * 1024ULL * 1024ULL;
  FragmentationStateTable table(/*zone_capacity_bytes=*/kZoneSize,
                                /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/4ULL * 1024ULL * 1024ULL);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.min_migrate_ratio = 0.03125f;
  ReorgPlanner planner(cfg, &table, /*zbd=*/nullptr);

  ASSERT_FALSE(planner.NextPlan().has_value());
  const std::string debug = planner.DebugString();
  ASSERT_NE(debug.find("min_migrate_bytes=8388608"), std::string::npos);
  ASSERT_NE(debug.find("tiny_plan_skip_count=1"), std::string::npos);
}

TEST(ReorgPlanner, AdaptiveTauIncreasesUnderForegroundPressure) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.tau_trigger_init = 10.0f;
  cfg.foreground_p99_target_us = 1000.0f;
  cfg.tau_pressure_gain = 0.5f;
  ReorgPlanner planner(cfg, &table, /*zbd=*/nullptr);

  const float before = planner.CurrentTauTrigger();
  planner.UpdateForegroundPressure(/*p99_read_us=*/5000.0f);

  ASSERT_GT(planner.CurrentTauTrigger(), before);
}

TEST(ReorgPlanner, ExecuteMigratesAllValidExtents) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/200);
  ReorgPlanner planner(TestConfig(), &table, /*zbd=*/nullptr);
  uint64_t migrated_bytes = 0;
  planner.SetExecuteFn([&](const ReorgPlanner::Plan& plan) {
    migrated_bytes += table.Snapshot(plan.victim_zone).valid_bytes;
    return IOStatus::OK();
  });

  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();
  ASSERT_TRUE(plan.has_value());
  ASSERT_TRUE(planner.Execute(*plan).ok());

  ASSERT_EQ(migrated_bytes, 200);
}

TEST(ReorgPlanner, ResetVictimAfterExecute) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/200);
  ReorgPlanner planner(TestConfig(), &table, /*zbd=*/nullptr);
  planner.SetExecuteFn([&](const ReorgPlanner::Plan& plan) {
    table.OnZoneReset(plan.victim_zone);
    return IOStatus::OK();
  });

  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();
  ASSERT_TRUE(plan.has_value());
  ASSERT_TRUE(planner.Execute(*plan).ok());

  ASSERT_EQ(table.Snapshot(0).valid_bytes, 0);
}

TEST(ReorgPlanner, ComputeNetVirtualOverridable) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/3);
  SeedReorgCandidates(&table);
  OverridePlanner planner(TestConfig(), &table, /*zbd=*/nullptr);

  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();

  ASSERT_TRUE(plan.has_value());
  ASSERT_EQ(plan->victim_zone, 2);
  ASSERT_EQ(plan->net_benefit, 1000000.0f);
}

TEST(ReorgPlanner, DebugStringReportsExecutionCounters) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/100);
  ReorgPlanner planner(TestConfig(), &table, /*zbd=*/nullptr);
  planner.SetExecuteFn([](const ReorgPlanner::Plan&) {
    return IOStatus::OK();
  });

  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();
  ASSERT_TRUE(plan.has_value());
  ASSERT_TRUE(planner.Execute(*plan).ok());

  ASSERT_NE(planner.DebugString().find("executed_plans=1"),
            std::string::npos);
}

#if FACO_ENABLE_LACR

TEST(ReorgPlanner, LacrDisabledLeavesM3NetAndTraceFieldsUnchanged) {
  ScopedEnv enabled("FACO_LACR_ENABLE", "1");
  FacoLacrState& lacr_state = GetFacoLacrState();
  lacr_state.ResetForTests();
  lacr_state.SetFileZoneMapFn([](const std::string& file) {
    if (file == "/000001.sst") {
      return std::vector<std::pair<uint64_t, uint64_t>>{{0, 800}};
    }
    return std::vector<std::pair<uint64_t, uint64_t>>{};
  });
  lacr_state.MarkCompactionBegin(
      MakeLacrCompaction(/*job_id=*/21, "/000001.sst", 800, 0));

  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/500);
  ExposedPlanner planner(TestConfig(), &table, /*zbd=*/nullptr);

  setenv("FACO_LACR_ENABLE", "0", /*overwrite=*/1);
  const float disabled_net = planner.NetForTest(0);
  std::optional<ReorgPlanner::Plan> plan = planner.NextPlan();

  lacr_state.ResetForTests();
  const float m3_net = planner.NetForTest(0);

  ASSERT_EQ(disabled_net, m3_net);
  ASSERT_TRUE(plan.has_value());
  ASSERT_EQ(plan->net_benefit, m3_net);
  ASSERT_EQ(planner.ExportTraceCsv().find("lacr_enabled"), std::string::npos);
}

TEST(ReorgPlanner, LacrActiveCompactionAppliesWastePenalty) {
  ScopedEnv enabled("FACO_LACR_ENABLE", "1");
  ScopedEnv synergy("FACO_LACR_W_SYNERGY", "0");
  ScopedEnv waste("FACO_LACR_W_WASTE", "1");
  ScopedEnv latency("FACO_LACR_W_LATENCY", "0");
  ScopedEnv active_cap("FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES", "1000000");
  FacoLacrState& lacr_state = GetFacoLacrState();
  lacr_state.ResetForTests();
  lacr_state.SetFileZoneMapFn([](const std::string& file) {
    if (file == "/000003.sst") {
      return std::vector<std::pair<uint64_t, uint64_t>>{{0, 800}};
    }
    return std::vector<std::pair<uint64_t, uint64_t>>{};
  });
  lacr_state.MarkCompactionBegin(
      MakeLacrCompaction(/*job_id=*/22, "/000003.sst", 800, 0));

  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/500);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.lacr_w_synergy = 0.0f;
  cfg.lacr_w_waste = 1.0f;
  cfg.lacr_w_latency = 0.0f;
  cfg.lacr_active_compaction_penalty_bytes = 1000000;
  ExposedPlanner planner(cfg, &table, /*zbd=*/nullptr);

  setenv("FACO_LACR_ENABLE", "0", /*overwrite=*/1);
  const float net_m3 = planner.NetForTest(0);
  setenv("FACO_LACR_ENABLE", "1", /*overwrite=*/1);
  const float net_m4 = planner.NetForTest(0);

  ASSERT_LT(net_m4, net_m3);
  planner.NextPlan();
  const std::string trace = planner.ExportTraceCsv();
  ASSERT_NE(trace.find("lacr_waste_penalty"), std::string::npos);
  ASSERT_NE(trace.find("net_m3"), std::string::npos);
  ASSERT_NE(trace.find("net_m4"), std::string::npos);
  lacr_state.ResetForTests();
}

TEST(ReorgPlanner, LacrRecentInvalidationCanApplySynergyBonus) {
  ScopedEnv enabled("FACO_LACR_ENABLE", "1");
  ScopedEnv synergy("FACO_LACR_W_SYNERGY", "1");
  ScopedEnv waste("FACO_LACR_W_WASTE", "0");
  ScopedEnv latency("FACO_LACR_W_LATENCY", "0");
  ScopedEnv bonus_cap("FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES", "1000000");
  FacoLacrState& lacr_state = GetFacoLacrState();
  lacr_state.ResetForTests();
  lacr_state.SetFileZoneMapFn([](const std::string& file) {
    if (file == "/000004.sst") {
      return std::vector<std::pair<uint64_t, uint64_t>>{{0, 1000}};
    }
    return std::vector<std::pair<uint64_t, uint64_t>>{};
  });
  CompactionJobInfo info =
      MakeLacrCompaction(/*job_id=*/23, "/000004.sst", 1000, 400);
  lacr_state.MarkCompactionBegin(info);
  lacr_state.MarkCompactionEnd(info);

  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/500);
  ReorgPlanner::Config cfg = TestConfig();
  cfg.lacr_w_synergy = 1.0f;
  cfg.lacr_w_waste = 0.0f;
  cfg.lacr_w_latency = 0.0f;
  cfg.lacr_recent_invalidation_bonus_bytes = 1000000;
  ExposedPlanner planner(cfg, &table, /*zbd=*/nullptr);

  setenv("FACO_LACR_ENABLE", "0", /*overwrite=*/1);
  const float net_m3 = planner.NetForTest(0);
  setenv("FACO_LACR_ENABLE", "1", /*overwrite=*/1);
  const float net_m4 = planner.NetForTest(0);

  ASSERT_GT(lacr_state.ZoneRecentInvalidationBytes(0), 0);
  ASSERT_GT(net_m4, net_m3);
  lacr_state.ResetForTests();
}

#endif  // FACO_ENABLE_LACR

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
