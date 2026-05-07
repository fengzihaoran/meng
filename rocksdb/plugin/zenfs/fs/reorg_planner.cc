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

#include "db/faco_lacr_state.h"
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

ReorgPlanner::TauMode ReadEnvTauMode(const char* name,
                                     ReorgPlanner::TauMode default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return default_value;
  const std::string mode(value);
  if (mode == "adaptive") return ReorgPlanner::TauMode::kAdaptive;
  if (mode == "fixed") return ReorgPlanner::TauMode::kFixed;
  return default_value;
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
  cfg.min_migrate_bytes = std::max<uint64_t>(0, cfg.min_migrate_bytes);
  cfg.min_migrate_ratio = ClampFloat(cfg.min_migrate_ratio, 0.0f, 1.0f);
  cfg.victim_cooldown_us = std::max<uint64_t>(0, cfg.victim_cooldown_us);
  cfg.adaptive_history_size = std::max<size_t>(1, cfg.adaptive_history_size);
  cfg.adaptive_q_min = ClampFloat(cfg.adaptive_q_min, 0.0f, 1.0f);
  cfg.adaptive_q_max =
      ClampFloat(std::max(cfg.adaptive_q_min, cfg.adaptive_q_max), 0.0f, 1.0f);
  cfg.adaptive_q_base =
      ClampFloat(cfg.adaptive_q_base, cfg.adaptive_q_min, cfg.adaptive_q_max);
  cfg.adaptive_q_budget_gain =
      ClampFloat(cfg.adaptive_q_budget_gain, 0.0f, 1.0f);
  cfg.accept_hysteresis = std::max(0.0f, cfg.accept_hysteresis);
  cfg.adaptive_warmup_evals =
      std::max<uint64_t>(0, cfg.adaptive_warmup_evals);
  cfg.min_exec_interval_us = std::max<uint64_t>(0, cfg.min_exec_interval_us);
  cfg.lacr_w_synergy = std::max(0.0f, cfg.lacr_w_synergy);
  cfg.lacr_w_waste = std::max(0.0f, cfg.lacr_w_waste);
  cfg.lacr_w_latency = std::max(0.0f, cfg.lacr_w_latency);
  cfg.lacr_active_compaction_penalty_bytes =
      std::max<uint64_t>(0, cfg.lacr_active_compaction_penalty_bytes);
  cfg.lacr_recent_invalidation_bonus_bytes =
      std::max<uint64_t>(0, cfg.lacr_recent_invalidation_bonus_bytes);
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
  cfg.tau_mode = ReadEnvTauMode("FACO_REORG_TAU_MODE", cfg.tau_mode);
  cfg.min_migrate_bytes =
      ReadEnvUint64("FACO_REORG_MIN_MIGRATE_BYTES", cfg.min_migrate_bytes);
  cfg.min_migrate_ratio =
      ReadEnvFloat("FACO_REORG_MIN_MIGRATE_RATIO", cfg.min_migrate_ratio);
  cfg.victim_cooldown_us = ReadEnvUint64(
      "FACO_REORG_VICTIM_COOLDOWN_US", cfg.victim_cooldown_us);
  cfg.adaptive_history_size = ReadEnvSize(
      "FACO_REORG_ADAPTIVE_HISTORY_SIZE", cfg.adaptive_history_size);
  cfg.adaptive_q_base =
      ReadEnvFloat("FACO_REORG_ADAPTIVE_Q_BASE", cfg.adaptive_q_base);
  cfg.adaptive_q_min =
      ReadEnvFloat("FACO_REORG_ADAPTIVE_Q_MIN", cfg.adaptive_q_min);
  cfg.adaptive_q_max =
      ReadEnvFloat("FACO_REORG_ADAPTIVE_Q_MAX", cfg.adaptive_q_max);
  cfg.adaptive_q_budget_gain = ReadEnvFloat(
      "FACO_REORG_ADAPTIVE_Q_BUDGET_GAIN", cfg.adaptive_q_budget_gain);
  cfg.accept_hysteresis =
      ReadEnvFloat("FACO_REORG_ACCEPT_HYSTERESIS", cfg.accept_hysteresis);
  cfg.adaptive_warmup_evals = ReadEnvUint64(
      "FACO_REORG_ADAPTIVE_WARMUP_EVALS", cfg.adaptive_warmup_evals);
  cfg.min_exec_interval_us = ReadEnvUint64(
      "FACO_REORG_MIN_EXEC_INTERVAL_US", cfg.min_exec_interval_us);
  cfg.lacr_w_synergy =
      ReadEnvFloat("FACO_LACR_W_SYNERGY", cfg.lacr_w_synergy);
  cfg.lacr_w_waste = ReadEnvFloat("FACO_LACR_W_WASTE", cfg.lacr_w_waste);
  cfg.lacr_w_latency =
      ReadEnvFloat("FACO_LACR_W_LATENCY", cfg.lacr_w_latency);
  cfg.lacr_active_compaction_penalty_bytes = ReadEnvUint64(
      "FACO_LACR_ACTIVE_COMPACTION_PENALTY_BYTES",
      cfg.lacr_active_compaction_penalty_bytes);
  cfg.lacr_recent_invalidation_bonus_bytes = ReadEnvUint64(
      "FACO_LACR_RECENT_INVALIDATION_BONUS_BYTES",
      cfg.lacr_recent_invalidation_bonus_bytes);
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
      eval_count_(0),
      no_candidate_count_(0),
      rejected_plans_(0),
      accepted_plans_(0),
      executed_plans_(0),
      migrated_extents_(0),
      migrated_bytes_(0),
      cooldown_skip_count_(0),
      tiny_plan_skip_count_(0),
      warmup_reject_count_(0),
      rate_limited_reject_count_(0),
      max_net_seen_(0.0f),
      last_adaptive_q_(0.0f),
      last_adaptive_tau_(0.0f),
      last_execution_us_(0) {
  trace_.reserve(128);
  adaptive_net_history_.reserve(cfg_.adaptive_history_size);
}

void ReorgPlanner::SetExecuteFn(ExecuteFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  execute_fn_ = std::move(fn);
}

std::optional<ReorgPlanner::Plan> ReorgPlanner::NextPlan() {
#if FACO_ENABLE_REORG && FACO_ENABLE_CFSM
  {
    std::lock_guard<std::mutex> lock(mutex_);
    eval_count_++;
  }

  if (frag_ == nullptr || zone_capacity_ == 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    no_candidate_count_++;
    return std::nullopt;
  }

  const std::vector<uint64_t> victims = frag_->RankVictimZones(cfg_.top_k);
  if (victims.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    no_candidate_count_++;
    return std::nullopt;
  }

  const uint64_t now_us = Env::Default()->NowMicros();
  Plan best;
  Plan best_observed;
  LacrAdjustment best_lacr;
  LacrAdjustment best_observed_lacr;
  bool found = false;
  bool observed = false;
  uint64_t cooldown_skipped = 0;
  uint64_t tiny_skipped = 0;
  const uint64_t min_migrate_bytes = EffectiveMinMigrateBytes();
  for (uint64_t zone_id : victims) {
    const ZoneFragState state = frag_->Snapshot(zone_id);
    const uint64_t valid = std::min(state.valid_bytes, zone_capacity_);
    const float net = ComputeNet(zone_id);
    if (!std::isfinite(net)) continue;
    const LacrAdjustment lacr_adjustment =
        ComputeLacrAdjustment(zone_id, ComputeM3Net(zone_id));

    const Plan candidate{zone_id, net, valid};
    if (!observed || net > best_observed.net_benefit ||
        (net == best_observed.net_benefit &&
         zone_id < best_observed.victim_zone)) {
      best_observed = candidate;
      best_observed_lacr = lacr_adjustment;
      observed = true;
    }

    if (min_migrate_bytes > 0 && valid < min_migrate_bytes) {
      tiny_skipped++;
      continue;
    }

    bool on_cooldown = false;
    if (cfg_.victim_cooldown_us > 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = zone_cooldown_until_us_.find(zone_id);
      if (it != zone_cooldown_until_us_.end()) {
        if (now_us < it->second) {
          on_cooldown = true;
        } else {
          zone_cooldown_until_us_.erase(it);
        }
      }
    }
    if (on_cooldown) {
      cooldown_skipped++;
      continue;
    }

    if (!found || net > best.net_benefit ||
        (net == best.net_benefit && zone_id < best.victim_zone)) {
      best = candidate;
      best_lacr = lacr_adjustment;
      found = true;
    }
  }

  if (!observed) {
    std::lock_guard<std::mutex> lock(mutex_);
    no_candidate_count_++;
    return std::nullopt;
  }

  std::string reason;
  if (!found) {
    if (tiny_skipped > 0 && cooldown_skipped > 0) {
      reason = "filtered_tiny_and_cooldown";
    } else if (tiny_skipped > 0) {
      reason = "filtered_tiny";
    } else if (cooldown_skipped > 0) {
      reason = "filtered_cooldown";
    } else {
      reason = "no_actionable_candidate";
    }
  }

  if (!found) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_net_seen_ = std::max(max_net_seen_, best_observed.net_benefit);
    tiny_plan_skip_count_ += tiny_skipped;
    cooldown_skip_count_ += cooldown_skipped;
    rejected_plans_++;
    last_plan_ = best_observed;
    const float tau_trigger = CurrentTauTriggerNoLock();
    RecordTraceLocked(now_us, best_observed, /*accepted=*/false,
                      cooldown_skipped, tiny_skipped, reason, tau_trigger,
                      last_adaptive_q_, last_adaptive_tau_,
                      /*warmup=*/false, /*rate_limited=*/false,
                      best_observed_lacr);
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  max_net_seen_ = std::max(max_net_seen_, best_observed.net_benefit);
  tiny_plan_skip_count_ += tiny_skipped;
  cooldown_skip_count_ += cooldown_skipped;
  float adaptive_q = 0.0f;
  float adaptive_tau = 0.0f;
  float tau_trigger = CurrentTauTriggerNoLock();
  bool warmup = false;
  bool rate_limited = false;
  std::string accept_reason = "accepted";

  if (cfg_.tau_mode == TauMode::kAdaptive) {
    adaptive_q = AdaptiveQuantileNoLock();
    const size_t history_before = adaptive_net_history_.size();
    warmup = history_before < cfg_.adaptive_warmup_evals;
    adaptive_tau = AdaptiveTauNoLock(adaptive_q);
    last_adaptive_q_ = adaptive_q;
    last_adaptive_tau_ = adaptive_tau;
    tau_trigger = adaptive_tau * (1.0f + cfg_.accept_hysteresis);
    if (warmup) {
      accept_reason = "warmup";
    }
  }

  if (!warmup && cfg_.min_exec_interval_us > 0 && last_execution_us_ > 0 &&
      now_us < last_execution_us_ + cfg_.min_exec_interval_us) {
    rate_limited = true;
    accept_reason = "rate_limited";
  }

  bool accepted = best.net_benefit > tau_trigger;
  if (warmup || rate_limited) {
    accepted = false;
  } else if (!accepted) {
    accept_reason = "below_tau";
  }
  if (cfg_.tau_mode == TauMode::kAdaptive) {
    RecordAdaptiveNetLocked(best.net_benefit);
  }

  RecordTraceLocked(now_us, best, accepted, cooldown_skipped, tiny_skipped,
                    accept_reason, tau_trigger, adaptive_q, adaptive_tau,
                    warmup, rate_limited, best_lacr);
  if (!accepted) {
    rejected_plans_++;
    if (warmup) {
      warmup_reject_count_++;
    }
    if (rate_limited) {
      rate_limited_reject_count_++;
    }
    last_plan_ = best;
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
    last_execution_us_ = Env::Default()->NowMicros();
    if (cfg_.victim_cooldown_us > 0) {
      zone_cooldown_until_us_[p.victim_zone] =
          last_execution_us_ + cfg_.victim_cooldown_us;
    }
  }
  return s;
#else
  (void)p;
  return IOStatus::OK();
#endif
}

void ReorgPlanner::RecordMigration(uint64_t extents, uint64_t bytes) {
#if FACO_ENABLE_REORG && FACO_ENABLE_CFSM
  std::lock_guard<std::mutex> lock(mutex_);
  migrated_extents_ += extents;
  migrated_bytes_ += bytes;
#else
  (void)extents;
  (void)bytes;
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
  const float displayed_tau =
      cfg_.tau_mode == TauMode::kAdaptive && last_adaptive_tau_ > 0.0f
          ? last_adaptive_tau_ * (1.0f + cfg_.accept_hysteresis)
          : CurrentTauTriggerNoLock();
  oss << "ReorgPlanner{tau_base=" << tau_trigger_
      << ", tau_effective=" << displayed_tau
      << ", tau_mode=" << TauModeName(cfg_.tau_mode)
      << ", top_k=" << cfg_.top_k
      << ", budget=" << CurrentBudget() << "/" << MaxActiveBudget()
      << ", min_migrate_bytes=" << EffectiveMinMigrateBytes()
      << ", min_migrate_ratio=" << cfg_.min_migrate_ratio
      << ", victim_cooldown_us=" << cfg_.victim_cooldown_us
      << ", adaptive_history_size=" << adaptive_net_history_.size()
      << "/" << cfg_.adaptive_history_size
      << ", adaptive_q_base=" << cfg_.adaptive_q_base
      << ", adaptive_q_last=" << last_adaptive_q_
      << ", adaptive_tau_last=" << last_adaptive_tau_
      << ", adaptive_warmup_evals=" << cfg_.adaptive_warmup_evals
      << ", accept_hysteresis=" << cfg_.accept_hysteresis
      << ", min_exec_interval_us=" << cfg_.min_exec_interval_us
      << ", eval_count=" << eval_count_
      << ", no_candidate_count=" << no_candidate_count_
      << ", rejected_plans=" << rejected_plans_
      << ", accepted_plans=" << accepted_plans_
      << ", executed_plans=" << executed_plans_
      << ", migrated_extents=" << migrated_extents_
      << ", migrated_bytes=" << migrated_bytes_
      << ", cooldown_skip_count=" << cooldown_skip_count_
      << ", tiny_plan_skip_count=" << tiny_plan_skip_count_
      << ", warmup_reject_count=" << warmup_reject_count_
      << ", rate_limited_reject_count=" << rate_limited_reject_count_
      << ", max_net_seen=" << max_net_seen_
      << ", last_victim=" << last_plan_.victim_zone
      << ", last_net=" << last_plan_.net_benefit
      << ", last_valid_bytes=" << last_plan_.estimated_valid_bytes
      << ", p99_ema_us=" << foreground_p99_ema_;
  if (LacrEnabled()) {
    oss << ", lacr_enabled=1"
        << ", lacr_w_synergy=" << cfg_.lacr_w_synergy
        << ", lacr_w_waste=" << cfg_.lacr_w_waste
        << ", lacr_w_latency=" << cfg_.lacr_w_latency
        << ", lacr_active_compaction_penalty_bytes="
        << cfg_.lacr_active_compaction_penalty_bytes
        << ", lacr_recent_invalidation_bonus_bytes="
        << cfg_.lacr_recent_invalidation_bonus_bytes;
  }
  oss << "}";
  return oss.str();
}

std::string ReorgPlanner::ExportTraceCsv() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "now_us,victim_zone,net_benefit,tau_trigger,accepted,"
         "tau_mode,adaptive_q,adaptive_tau,warmup,rate_limited,"
         "current_budget,max_active_budget,estimated_valid_bytes,"
         "cooldown_skipped,tiny_skipped,history_size,reason";
  const bool lacr_enabled = LacrEnabled();
  if (lacr_enabled) {
    oss << ",lacr_enabled,lacr_zone_score,lacr_synergy_bonus,"
           "lacr_waste_penalty,lacr_latency_penalty,net_m3,net_m4,"
           "active_compaction_files,compaction_touched_zone";
  }
  oss << "\n";
  oss << std::setprecision(9);
  for (const TraceSample& sample : trace_) {
    oss << sample.now_us << "," << sample.victim_zone << ","
        << sample.net_benefit << "," << sample.tau_trigger << ","
        << sample.accepted << "," << sample.tau_mode << ","
        << sample.adaptive_q << "," << sample.adaptive_tau << ","
        << sample.warmup << "," << sample.rate_limited << ","
        << sample.current_budget << "," << sample.max_active_budget << ","
        << sample.estimated_valid_bytes << "," << sample.cooldown_skipped
        << "," << sample.tiny_skipped << "," << sample.history_size << ","
        << sample.reason;
    if (lacr_enabled) {
      oss << "," << sample.lacr_enabled << "," << sample.lacr_zone_score
          << "," << sample.lacr_synergy_bonus << ","
          << sample.lacr_waste_penalty << "," << sample.lacr_latency_penalty
          << "," << sample.net_m3 << "," << sample.net_m4 << ","
          << sample.active_compaction_files << ","
          << sample.compaction_touched_zone;
    }
    oss << "\n";
  }
  return oss.str();
}

float ReorgPlanner::ComputeNet(uint64_t zone_id) const {
  const float net_m3 = ComputeM3Net(zone_id);
  return ComputeLacrAdjustment(zone_id, net_m3).net_m4;
}

bool ReorgPlanner::LacrEnabled() const {
  return FacoLacrRuntimeEnabled();
}

float ReorgPlanner::ComputeM3Net(uint64_t zone_id) const {
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

ReorgPlanner::LacrAdjustment ReorgPlanner::ComputeLacrAdjustment(
    uint64_t zone_id, float net_m3) const {
  LacrAdjustment adjustment;
  adjustment.net_m3 = net_m3;
  adjustment.net_m4 = net_m3;

#if FACO_ENABLE_LACR
  if (!LacrEnabled() || !std::isfinite(net_m3)) {
    return adjustment;
  }

  const FacoLacrState& lacr_state = GetFacoLacrState();
  const uint64_t active_bytes =
      lacr_state.ZoneActiveCompactionBytes(zone_id);
  const uint64_t recent_invalidated_bytes =
      lacr_state.ZoneRecentInvalidationBytes(zone_id);
  const uint64_t active_penalty_cap =
      cfg_.lacr_active_compaction_penalty_bytes;
  const uint64_t waste_base =
      active_penalty_cap == 0
          ? active_bytes
          : std::min(active_bytes, active_penalty_cap);
  const uint64_t recent_bonus_cap =
      cfg_.lacr_recent_invalidation_bonus_bytes;
  const uint64_t bonus_base =
      recent_bonus_cap == 0
          ? recent_invalidated_bytes
          : std::min(recent_invalidated_bytes, recent_bonus_cap);
  const uint64_t latency_base = active_bytes == 0 ? 0 : waste_base;

  adjustment.enabled = 1;
  adjustment.zone_score =
      static_cast<float>(lacr_state.ZoneCompactionScore(zone_id));
  adjustment.synergy_bonus =
      cfg_.lacr_w_synergy * static_cast<float>(bonus_base);
  adjustment.waste_penalty = cfg_.lacr_w_waste * static_cast<float>(waste_base);
  adjustment.latency_penalty =
      cfg_.lacr_w_latency * static_cast<float>(latency_base);
  adjustment.active_compaction_files = lacr_state.ActiveCompactionFiles();
  adjustment.compaction_touched_zone =
      lacr_state.WasZoneTouchedByCompaction(zone_id) ? 1 : 0;
  adjustment.net_m4 = net_m3 + adjustment.synergy_bonus -
                      adjustment.waste_penalty - adjustment.latency_penalty;
#else
  (void)zone_id;
#endif

  return adjustment;
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

float ReorgPlanner::BudgetRatioForAdaptive() const {
  const int max_budget = MaxActiveBudget();
  if (max_budget <= 0) return 1.0f;
  return ClampFloat(
      static_cast<float>(CurrentBudget()) / static_cast<float>(max_budget),
      0.0f, 1.0f);
}

const char* ReorgPlanner::TauModeName(TauMode mode) {
  return mode == TauMode::kAdaptive ? "adaptive" : "fixed";
}

uint64_t ReorgPlanner::EffectiveMinMigrateBytes() const {
  if (cfg_.min_migrate_bytes > 0 || zone_capacity_ == 0) {
    return cfg_.min_migrate_bytes;
  }
  const double derived =
      static_cast<double>(zone_capacity_) * cfg_.min_migrate_ratio;
  if (derived <= 0.0) return 0;
  return static_cast<uint64_t>(std::ceil(derived));
}

float ReorgPlanner::AdaptiveQuantileNoLock() const {
  const float budget_ratio = BudgetRatioForAdaptive();
  const float q = cfg_.adaptive_q_base -
                  cfg_.adaptive_q_budget_gain * (1.0f - budget_ratio);
  return ClampFloat(q, cfg_.adaptive_q_min, cfg_.adaptive_q_max);
}

float ReorgPlanner::AdaptiveTauNoLock(float q) const {
  if (adaptive_net_history_.empty()) return CurrentTauTriggerNoLock();

  std::vector<float> values = adaptive_net_history_;
  std::sort(values.begin(), values.end());
  if (values.size() == 1) return values[0];

  const float clamped_q = ClampFloat(q, 0.0f, 1.0f);
  const float pos = clamped_q * static_cast<float>(values.size() - 1);
  const size_t lo = static_cast<size_t>(std::floor(pos));
  const size_t hi = std::min(values.size() - 1, lo + 1);
  const float frac = pos - static_cast<float>(lo);
  return values[lo] * (1.0f - frac) + values[hi] * frac;
}

void ReorgPlanner::RecordAdaptiveNetLocked(float net_benefit) {
  if (!std::isfinite(net_benefit)) return;
  if (adaptive_net_history_.size() == cfg_.adaptive_history_size) {
    adaptive_net_history_.erase(adaptive_net_history_.begin());
  }
  adaptive_net_history_.push_back(net_benefit);
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
  const int budget_max = zbd_->GetMaxZoneBudget();
  if (budget_max > 0) return budget_max;
  return static_cast<int>(zbd_->GetMaxActiveIOZoneLimit());
}

void ReorgPlanner::RecordTraceLocked(uint64_t now_us, const Plan& plan,
                                     bool accepted,
                                     uint64_t cooldown_skipped,
                                     uint64_t tiny_skipped,
                                     const std::string& reason,
                                     float tau_trigger, float adaptive_q,
                                     float adaptive_tau, bool warmup,
                                     bool rate_limited,
                                     const LacrAdjustment& lacr_adjustment) {
  if (trace_.size() == kMaxTraceSamples) {
    trace_.erase(trace_.begin());
  }
  TraceSample sample;
  sample.now_us = now_us;
  sample.victim_zone = plan.victim_zone;
  sample.net_benefit = plan.net_benefit;
  sample.tau_trigger = tau_trigger;
  sample.tau_mode = TauModeName(cfg_.tau_mode);
  sample.adaptive_q = adaptive_q;
  sample.adaptive_tau = adaptive_tau;
  sample.accepted = accepted ? 1 : 0;
  sample.warmup = warmup ? 1 : 0;
  sample.rate_limited = rate_limited ? 1 : 0;
  sample.current_budget = CurrentBudget();
  sample.max_active_budget = MaxActiveBudget();
  sample.history_size = adaptive_net_history_.size();
  sample.estimated_valid_bytes = plan.estimated_valid_bytes;
  sample.cooldown_skipped = cooldown_skipped;
  sample.tiny_skipped = tiny_skipped;
  sample.reason = reason;
  sample.lacr_enabled = lacr_adjustment.enabled;
  sample.lacr_zone_score = lacr_adjustment.zone_score;
  sample.lacr_synergy_bonus = lacr_adjustment.synergy_bonus;
  sample.lacr_waste_penalty = lacr_adjustment.waste_penalty;
  sample.lacr_latency_penalty = lacr_adjustment.latency_penalty;
  sample.net_m3 = lacr_adjustment.net_m3;
  sample.net_m4 = lacr_adjustment.net_m4;
  sample.active_compaction_files = lacr_adjustment.active_compaction_files;
  sample.compaction_touched_zone = lacr_adjustment.compaction_touched_zone;
  trace_.push_back(sample);
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
