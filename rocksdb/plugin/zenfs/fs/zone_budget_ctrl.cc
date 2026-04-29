// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "zone_budget_ctrl.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "frag_state_table.h"

namespace ROCKSDB_NAMESPACE {

namespace {

constexpr float kMinDenom = 1e-6f;
constexpr size_t kMaxTraceSamples = 4096;

float ClampFloat(float value, float lo, float hi) {
  return std::max(lo, std::min(hi, value));
}

#if FACO_ENABLE_BUDGET
int ClampInt(int value, int lo, int hi) {
  return std::max(lo, std::min(hi, value));
}

int RoundBudget(float budget) {
  return static_cast<int>(std::lround(budget));
}
#endif  // FACO_ENABLE_BUDGET

}  // namespace

ZoneBudgetCtrl::Config ZoneBudgetCtrl::NormalizeConfig(Config cfg) {
  cfg.B_min = std::max(1, cfg.B_min);
  cfg.B_max = std::max(cfg.B_min, cfg.B_max);
  cfg.Kp = std::max(0.0f, cfg.Kp);
  cfg.Ki = std::max(0.0f, cfg.Ki);
  cfg.P_target = std::max(0.0f, cfg.P_target);
  cfg.theta_zvdr = std::max(0.0f, cfg.theta_zvdr);
  cfg.top_k = std::max<size_t>(1, cfg.top_k);
  cfg.rbd_threshold = ClampFloat(cfg.rbd_threshold, 0.0f, 0.999f);
  cfg.rbd_weight = std::max(0.0f, cfg.rbd_weight);
  cfg.zvdr_weight = std::max(0.0f, cfg.zvdr_weight);
  if (cfg.rbd_weight == 0.0f && cfg.zvdr_weight == 0.0f) {
    cfg.rbd_weight = 1.0f;
  }
  if (cfg.integral_min > cfg.integral_max) {
    std::swap(cfg.integral_min, cfg.integral_max);
  }
  return cfg;
}

ZoneBudgetCtrl::ZoneBudgetCtrl(const Config& cfg,
                               FragmentationStateTable* frag)
    : cfg_(NormalizeConfig(cfg)),
      frag_(frag),
      current_budget_(cfg_.B_max),
      budget_float_(static_cast<float>(cfg_.B_max)),
      integral_(0.0f),
      current_p_frag_(0.0f),
      current_avg_rbd_(0.0f),
      current_max_rbd_(0.0f),
      current_trend_pressure_(0.0f),
      current_victim_count_(0),
      last_update_us_(0) {
  trace_.reserve(128);
}

int ZoneBudgetCtrl::GetCurrentAllocBudget() const {
#if FACO_ENABLE_BUDGET
  return current_budget_.load(std::memory_order_relaxed);
#else
  return cfg_.B_max;
#endif
}

void ZoneBudgetCtrl::Update(uint64_t now_us) {
#if FACO_ENABLE_BUDGET
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_update_us_ != 0 && cfg_.update_interval_us != 0 &&
        now_us - last_update_us_ < cfg_.update_interval_us) {
      return;
    }
    last_update_us_ = now_us;
  }

  const PressureStats stats = ComputePressure();
  std::lock_guard<std::mutex> lock(mutex_);

  current_p_frag_ = stats.p_frag;
  current_avg_rbd_ = stats.avg_rbd;
  current_max_rbd_ = stats.max_rbd;
  current_trend_pressure_ = stats.trend_pressure;
  current_victim_count_ = stats.victim_count;

  const float error = cfg_.P_target - stats.p_frag;
  if (stats.p_frag <= cfg_.P_target) {
    // With P_target=0, a pure PI loop cannot expand after a previous shrink
    // because pressure is non-negative. Clear negative integral debt once
    // pressure is back at target so the budget can recover toward B_max.
    integral_ = std::max(0.0f, integral_ + error);
  } else {
    integral_ = ClampFloat(integral_ + error, cfg_.integral_min,
                           cfg_.integral_max);
  }

  const float delta = cfg_.Kp * error + cfg_.Ki * integral_;
  budget_float_ =
      ClampFloat(budget_float_ + delta, static_cast<float>(cfg_.B_min),
                 static_cast<float>(cfg_.B_max));

  int next_budget = ClampInt(RoundBudget(budget_float_), cfg_.B_min,
                             cfg_.B_max);
  if (stats.p_frag <= cfg_.P_target && next_budget < cfg_.B_max &&
      std::fabs(delta) < 0.5f) {
    next_budget++;
    budget_float_ = static_cast<float>(next_budget);
  }

  current_budget_.store(next_budget, std::memory_order_relaxed);
  RecordTraceLocked(now_us, stats, next_budget);
#else
  (void)now_us;
#endif
}

float ZoneBudgetCtrl::CurrentPFrag() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_p_frag_;
}

std::string ZoneBudgetCtrl::DebugString() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "ZoneBudgetCtrl{budget=" << current_budget_.load()
      << ", B_min=" << cfg_.B_min << ", B_max=" << cfg_.B_max
      << ", p_frag=" << current_p_frag_
      << ", avg_rbd=" << current_avg_rbd_
      << ", max_rbd=" << current_max_rbd_
      << ", trend_pressure=" << current_trend_pressure_
      << ", victim_count=" << current_victim_count_
      << ", integral=" << integral_ << "}";
  return oss.str();
}

std::string ZoneBudgetCtrl::ExportTraceCsv() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "now_us,budget,p_frag,avg_rbd,max_rbd,trend_pressure,"
         "victim_count\n";
  oss << std::setprecision(9);
  for (const TraceSample& sample : trace_) {
    oss << sample.now_us << "," << sample.budget << "," << sample.p_frag
        << "," << sample.avg_rbd << "," << sample.max_rbd << ","
        << sample.trend_pressure << "," << sample.victim_count << "\n";
  }
  return oss.str();
}

ZoneBudgetCtrl::PressureStats ZoneBudgetCtrl::ComputePressure() const {
  PressureStats stats;
  if (frag_ == nullptr) {
    return stats;
  }

  const std::vector<uint64_t> victims = frag_->RankVictimZones(cfg_.top_k);
  if (victims.empty()) {
    return stats;
  }

  double pressure_sum = 0.0;
  double rbd_sum = 0.0;
  double trend_sum = 0.0;
  const float rbd_scale = std::max(kMinDenom, 1.0f - cfg_.rbd_threshold);
  const float weight_sum =
      std::max(kMinDenom, cfg_.rbd_weight + cfg_.zvdr_weight);

  for (uint64_t zone_id : victims) {
    const float rbd = frag_->GetRBD(zone_id);
    const ZoneFragState snapshot = frag_->Snapshot(zone_id);
    const float rbd_pressure =
        std::max(0.0f, rbd - cfg_.rbd_threshold) / rbd_scale;
    const float trend_pressure =
        std::max(0.0f, snapshot.zvdr_ema - cfg_.theta_zvdr);
    const float zone_pressure =
        (cfg_.rbd_weight * rbd_pressure +
         cfg_.zvdr_weight * trend_pressure) /
        weight_sum;

    pressure_sum += zone_pressure;
    rbd_sum += rbd;
    trend_sum += trend_pressure;
    stats.max_rbd = std::max(stats.max_rbd, rbd);
  }

  stats.victim_count = victims.size();
  stats.p_frag = static_cast<float>(pressure_sum / victims.size());
  stats.avg_rbd = static_cast<float>(rbd_sum / victims.size());
  stats.trend_pressure = static_cast<float>(trend_sum / victims.size());
  return stats;
}

void ZoneBudgetCtrl::RecordTraceLocked(uint64_t now_us,
                                       const PressureStats& stats,
                                       int budget) {
  if (trace_.size() == kMaxTraceSamples) {
    trace_.erase(trace_.begin());
  }
  trace_.push_back(TraceSample{now_us, budget, stats.p_frag, stats.avg_rbd,
                               stats.max_rbd, stats.trend_pressure,
                               stats.victim_count});
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
