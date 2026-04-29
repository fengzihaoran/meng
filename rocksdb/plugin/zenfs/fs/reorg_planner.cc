// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "reorg_planner.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#include "frag_state_table.h"
#include "rocksdb/env.h"
#include "zbd_zenfs.h"

namespace ROCKSDB_NAMESPACE {

namespace {

constexpr size_t kMaxTraceSamples = 4096;

float ClampFloat(float value, float lo, float hi) {
  return std::max(lo, std::min(hi, value));
}

float ReadEnvFloat(const char* name, float default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return default_value;
  char* end = nullptr;
  const float parsed = std::strtof(value, &end);
  if (end == value || !std::isfinite(parsed)) return default_value;
  return parsed;
}

uint64_t ReadEnvUint64(const char* name, uint64_t default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return default_value;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (end == value) return default_value;
  return static_cast<uint64_t>(parsed);
}

size_t ReadEnvSize(const char* name, size_t default_value) {
  const uint64_t parsed = ReadEnvUint64(name, default_value);
  return parsed == 0 ? default_value : static_cast<size_t>(parsed);
}

}  // namespace

ReorgPlanner::Config ReorgPlanner::NormalizeConfig(Config cfg) {
  cfg.w1 = std::max(0.0f, cfg.w1);
  cfg.w2 = std::max(0.0f, cfg.w2);
  cfg.w3 = std::max(0.0f, cfg.w3);
  cfg.w4 = std::max(0.0f, cfg.w4);
  cfg.WA_factor = std::max(0.0f, cfg.WA_factor);
  cfg.T_horizon_us = std::max<uint64_t>(1, cfg.T_horizon_us);
  cfg.tau_trigger_init = std::max(0.0f, cfg.tau_trigger_init);
  cfg.top_k = std::max<size_t>(1, cfg.top_k);
  cfg.tau_min = std::max(0.0f, cfg.tau_min);
  cfg.tau_max = std::max(cfg.tau_min, cfg.tau_max);
  cfg.tau_trigger_init =
      ClampFloat(cfg.tau_trigger_init, cfg.tau_min, cfg.tau_max);
  cfg.tau_ema_alpha = ClampFloat(cfg.tau_ema_alpha, 0.0f, 1.0f);
  cfg.foreground_p99_target_us =
      std::max(1.0f, cfg.foreground_p99_target_us);
  cfg.tau_pressure_gain = std::max(0.0f, cfg.tau_pressure_gain);
  cfg.contention_penalty_bytes =
      std::max(0.0f, cfg.contention_penalty_bytes);
  return cfg;
}

ReorgPlanner::Config ReorgPlanner::LoadConfigFromEnv() {
  Config cfg;
  cfg.w1 = ReadEnvFloat("FACO_REORG_W1", cfg.w1);
  cfg.w2 = ReadEnvFloat("FACO_REORG_W2", cfg.w2);
  cfg.w3 = ReadEnvFloat("FACO_REORG_W3", cfg.w3);
  cfg.w4 = ReadEnvFloat("FACO_REORG_W4", cfg.w4);
  cfg.WA_factor = ReadEnvFloat("FACO_REORG_WA_FACTOR", cfg.WA_factor);
  cfg.T_horizon_us =
      ReadEnvUint64("FACO_REORG_T_HORIZON_US", cfg.T_horizon_us);
  cfg.tau_trigger_init =
      ReadEnvFloat("FACO_REORG_TAU_TRIGGER_INIT", cfg.tau_trigger_init);
  cfg.top_k = ReadEnvSize("FACO_REORG_TOP_K", cfg.top_k);
  cfg.tau_min = ReadEnvFloat("FACO_REORG_TAU_MIN", cfg.tau_min);
  cfg.tau_max = ReadEnvFloat("FACO_REORG_TAU_MAX", cfg.tau_max);
  cfg.tau_ema_alpha =
      ReadEnvFloat("FACO_REORG_TAU_EMA_ALPHA", cfg.tau_ema_alpha);
  cfg.foreground_p99_target_us = ReadEnvFloat(
      "FACO_REORG_FOREGROUND_P99_TARGET_US", cfg.foreground_p99_target_us);
  cfg.tau_pressure_gain =
      ReadEnvFloat("FACO_REORG_TAU_PRESSURE_GAIN", cfg.tau_pressure_gain);
  cfg.contention_penalty_bytes = ReadEnvFloat(
      "FACO_REORG_CONTENTION_PENALTY_BYTES", cfg.contention_penalty_bytes);
  return NormalizeConfig(cfg);
}

ReorgPlanner::ReorgPlanner(const Config& cfg, FragmentationStateTable* frag,
                           ZonedBlockDevice* zbd)
    : cfg_(NormalizeConfig(cfg)),
      frag_(frag),
      zbd_(zbd),
      zone_capacity_(frag != nullptr ? frag->ZoneCapacityBytes() : 0),
      tau_trigger_(cfg_.tau_trigger_init),
      foreground_p99_ema_(0.0f),
      accepted_plans_(0),
      executed_plans_(0) {
  trace_.reserve(128);
}

void ReorgPlanner::SetExecuteFn(ExecuteFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  execute_fn_ = std::move(fn);
}

std::optional<ReorgPlanner::Plan> ReorgPlanner::NextPlan() {
#if FACO_ENABLE_REORG && FACO_ENABLE_CFSM
  if (frag_ == nullptr || zone_capacity_ == 0) {
    return std::nullopt;
  }

  const std::vector<uint64_t> victims = frag_->RankVictimZones(cfg_.top_k);
  if (victims.empty()) {
    return std::nullopt;
  }

  Plan best;
  bool found = false;
  for (uint64_t zone_id : victims) {
    const float net = ComputeNet(zone_id);
    if (!std::isfinite(net)) continue;
    if (!found || net > best.net_benefit ||
        (net == best.net_benefit && zone_id < best.victim_zone)) {
      best = Plan{zone_id, net};
      found = true;
    }
  }

  if (!found) {
    return std::nullopt;
  }

  const uint64_t now_us = Env::Default()->NowMicros();
  std::lock_guard<std::mutex> lock(mutex_);
  const bool accepted = best.net_benefit > CurrentTauTriggerNoLock();
  RecordTraceLocked(now_us, best, accepted);
  if (!accepted) {
    return std::nullopt;
  }

  accepted_plans_++;
  last_plan_ = best;
  return best;
#else
  return std::nullopt;
#endif
}

IOStatus ReorgPlanner::Execute(const Plan& p) {
#if FACO_ENABLE_REORG && FACO_ENABLE_CFSM
  ExecuteFn fn;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    fn = execute_fn_;
  }
  if (!fn) {
    return IOStatus::InvalidArgument("FACO reorg execute callback is not set");
  }

  IOStatus s = fn(p);
  if (s.ok()) {
    std::lock_guard<std::mutex> lock(mutex_);
    executed_plans_++;
  }
  return s;
#else
  (void)p;
  return IOStatus::OK();
#endif
}

void ReorgPlanner::UpdateForegroundPressure(float p99_read_us) {
  if (!std::isfinite(p99_read_us) || p99_read_us < 0.0f) return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (foreground_p99_ema_ == 0.0f) {
    foreground_p99_ema_ = p99_read_us;
  } else {
    foreground_p99_ema_ =
        cfg_.tau_ema_alpha * p99_read_us +
        (1.0f - cfg_.tau_ema_alpha) * foreground_p99_ema_;
  }

  const float normalized_error =
      (foreground_p99_ema_ - cfg_.foreground_p99_target_us) /
      cfg_.foreground_p99_target_us;
  const float multiplier =
      1.0f + cfg_.tau_pressure_gain * normalized_error;
  tau_trigger_ =
      ClampFloat(tau_trigger_ * std::max(0.1f, multiplier), cfg_.tau_min,
                 cfg_.tau_max);
}

float ReorgPlanner::CurrentTauTrigger() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return CurrentTauTriggerNoLock();
}

std::string ReorgPlanner::DebugString() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "ReorgPlanner{tau_base=" << tau_trigger_
      << ", tau_effective=" << CurrentTauTriggerNoLock()
      << ", top_k=" << cfg_.top_k
      << ", budget=" << CurrentBudget() << "/" << MaxActiveBudget()
      << ", accepted_plans=" << accepted_plans_
      << ", executed_plans=" << executed_plans_
      << ", last_victim=" << last_plan_.victim_zone
      << ", last_net=" << last_plan_.net_benefit
      << ", p99_ema_us=" << foreground_p99_ema_ << "}";
  return oss.str();
}

std::string ReorgPlanner::ExportTraceCsv() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "now_us,victim_zone,net_benefit,tau_trigger,accepted,"
         "current_budget,max_active_budget\n";
  oss << std::setprecision(9);
  for (const TraceSample& sample : trace_) {
    oss << sample.now_us << "," << sample.victim_zone << ","
        << sample.net_benefit << "," << sample.tau_trigger << ","
        << sample.accepted << "," << sample.current_budget << ","
        << sample.max_active_budget << "\n";
  }
  return oss.str();
}

float ReorgPlanner::ComputeNet(uint64_t zone_id) const {
  if (frag_ == nullptr || zone_capacity_ == 0) {
    return -std::numeric_limits<float>::infinity();
  }

  const ZoneFragState state = frag_->Snapshot(zone_id);
  const uint64_t valid = std::min(state.valid_bytes, zone_capacity_);
  const uint64_t invalid = zone_capacity_ - valid;
  const float valid_ratio =
      static_cast<float>(valid) / static_cast<float>(zone_capacity_);
  const float benefit_immediate =
      cfg_.w1 * static_cast<float>(invalid);
  const float benefit_trend =
      cfg_.w2 * (1.0f - valid_ratio) * state.zvdr_ema *
      static_cast<float>(cfg_.T_horizon_us) / 1000000.0f;
  const float cost_migrate =
      cfg_.w3 * static_cast<float>(valid) * cfg_.WA_factor;
  const float cost_contention = cfg_.w4 * ContentionPenalty(zone_id);
  return benefit_immediate + benefit_trend - cost_migrate - cost_contention;
}

float ReorgPlanner::ContentionPenalty(uint64_t zone_id) const {
  (void)zone_id;
  const int max_budget = MaxActiveBudget();
  if (max_budget <= 0) return 0.0f;

  const int current_budget = CurrentBudget();
  const float budget_ratio = ClampFloat(
      static_cast<float>(current_budget) / static_cast<float>(max_budget),
      0.0f, 1.0f);
  return cfg_.contention_penalty_bytes * (1.0f - budget_ratio);
}

float ReorgPlanner::CurrentTauTriggerNoLock() const {
  return ClampFloat(tau_trigger_ * BudgetTauScale(), cfg_.tau_min,
                    cfg_.tau_max);
}

float ReorgPlanner::BudgetTauScale() const {
  const int max_budget = MaxActiveBudget();
  if (max_budget <= 0) return 1.0f;

  const float budget_ratio = ClampFloat(
      static_cast<float>(CurrentBudget()) / static_cast<float>(max_budget),
      0.0f, 1.0f);
  // A lower M2 budget means EBCR already sees fragmentation pressure, so M3
  // lowers tau modestly and becomes more willing to reorganize top victims.
  return ClampFloat(0.5f + 0.5f * budget_ratio, 0.5f, 1.0f);
}

int ReorgPlanner::CurrentBudget() const {
  if (zbd_ == nullptr) return MaxActiveBudget();
  return zbd_->GetCurrentZoneBudget();
}

int ReorgPlanner::MaxActiveBudget() const {
  if (zbd_ == nullptr) return 0;
  return static_cast<int>(zbd_->GetMaxActiveIOZoneLimit());
}

void ReorgPlanner::RecordTraceLocked(uint64_t now_us, const Plan& plan,
                                     bool accepted) {
  if (trace_.size() == kMaxTraceSamples) {
    trace_.erase(trace_.begin());
  }
  trace_.push_back(TraceSample{
      now_us, plan.victim_zone, plan.net_benefit, CurrentTauTriggerNoLock(),
      accepted ? 1 : 0, CurrentBudget(), MaxActiveBudget()});
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
