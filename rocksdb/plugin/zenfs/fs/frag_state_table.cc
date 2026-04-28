// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "frag_state_table.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <sstream>

namespace ROCKSDB_NAMESPACE {

namespace {

constexpr float kRbdSpaceWeight = 0.6f;
constexpr float kRbdTrendWeight = 0.4f;
constexpr float kHighInvalidRatio = 0.5f;
constexpr float kHotZvdr = 1e-12f;
constexpr uint64_t kMinZvdrElapsedUs = 1000;

}  // namespace

FragmentationStateTable::FragmentationStateTable(
    uint64_t zone_capacity_bytes, size_t num_zones, float ema_alpha)
    : zone_capacity_bytes_(zone_capacity_bytes),
      ema_alpha_(std::max(0.0f, std::min(1.0f, ema_alpha))),
      states_(num_zones) {
  for (size_t i = 0; i < states_.size(); ++i) {
    states_[i].zone_id = i;
  }
}

bool FragmentationStateTable::IsValidZone(uint64_t zone_id) const {
  return zone_id < states_.size() && zone_capacity_bytes_ > 0;
}

void FragmentationStateTable::OnAppend(uint64_t zone_id, uint64_t bytes) {
#if FACO_ENABLE_CFSM
  if (bytes == 0) return;

  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidZone(zone_id)) return;

  ZoneFragState& state = states_[zone_id];
  const uint64_t before = state.valid_bytes;
  const uint64_t room = zone_capacity_bytes_ - state.valid_bytes;
  state.valid_bytes = bytes >= room ? zone_capacity_bytes_
                                    : state.valid_bytes + bytes;

  // Appends create new live data rather than decay.  Move the previous
  // reference forward when validity grows so Tick does not treat growth as a
  // negative decay sample.
  if (state.valid_bytes > before && state.valid_bytes > state.valid_bytes_prev) {
    state.valid_bytes_prev = state.valid_bytes;
  }
  UpdateFragmentClass(&state);
#else
  (void)zone_id;
  (void)bytes;
#endif
}

void FragmentationStateTable::OnDelete(uint64_t zone_id, uint64_t bytes) {
#if FACO_ENABLE_CFSM
  if (bytes == 0) return;

  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidZone(zone_id)) return;

  ZoneFragState& state = states_[zone_id];
  if (bytes >= state.valid_bytes) {
    state.valid_bytes = 0;
  } else {
    state.valid_bytes -= bytes;
  }
  UpdateFragmentClass(&state);
#else
  (void)zone_id;
  (void)bytes;
#endif
}

void FragmentationStateTable::OnZoneReset(uint64_t zone_id) {
#if FACO_ENABLE_CFSM
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidZone(zone_id)) return;

  states_[zone_id] = ZoneFragState();
  states_[zone_id].zone_id = zone_id;
#else
  (void)zone_id;
#endif
}

void FragmentationStateTable::Tick(uint64_t now_us) {
#if FACO_ENABLE_CFSM
  std::unique_lock<std::shared_mutex> lock(mutex_);

  for (ZoneFragState& state : states_) {
    if (state.last_update_us == 0) {
      state.last_update_us = now_us;
      state.valid_bytes_prev = state.valid_bytes;
      UpdateFragmentClass(&state);
      continue;
    }

    // Delete and reset callbacks can happen inside the same microsecond on a
    // fast benchmark.  Use a small minimum window so such short-lived validity
    // decay is still observable without letting zero-time samples explode.
    const uint64_t raw_elapsed_us =
        now_us > state.last_update_us ? now_us - state.last_update_us : 0;
    const uint64_t elapsed_us = std::max(raw_elapsed_us, kMinZvdrElapsedUs);
    float zvdr_inst = 0.0f;
    if (state.valid_bytes_prev > state.valid_bytes) {
      const double decay_bytes =
          static_cast<double>(state.valid_bytes_prev - state.valid_bytes);
      const double denom = static_cast<double>(elapsed_us) *
                           static_cast<double>(zone_capacity_bytes_);
      zvdr_inst = static_cast<float>(decay_bytes / denom);
      if (!std::isfinite(zvdr_inst)) {
        zvdr_inst = 0.0f;
      }
    }

    state.zvdr_ema =
        ema_alpha_ * zvdr_inst + (1.0f - ema_alpha_) * state.zvdr_ema;
    state.valid_bytes_prev = state.valid_bytes;
    state.last_update_us = now_us;
    UpdateFragmentClass(&state);
  }
#else
  (void)now_us;
#endif
}

float FragmentationStateTable::GetZVDR(uint64_t zone_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidZone(zone_id)) return 0.0f;
  return states_[zone_id].zvdr_ema;
}

float FragmentationStateTable::GetRBD(uint64_t zone_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidZone(zone_id)) return 0.0f;
  return GetRBDNoLock(states_[zone_id]);
}

void FragmentationStateTable::SetFileZoneMapFn(FileZoneMapFn fn) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  file_zone_map_fn_ = std::move(fn);
}

float FragmentationStateTable::GetFFD(const std::string& filename) const {
  FileZoneMapFn map_fn;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    map_fn = file_zone_map_fn_;
  }

  if (!map_fn) return 0.0f;

  const std::vector<std::pair<uint64_t, uint64_t>> extents = map_fn(filename);
  uint64_t file_bytes = 0;
  for (const auto& entry : extents) {
    file_bytes += entry.second;
  }
  if (file_bytes == 0) return 0.0f;

  std::shared_lock<std::shared_mutex> lock(mutex_);
  double weighted_density = 0.0;
  for (const auto& entry : extents) {
    const uint64_t zone_id = entry.first;
    const uint64_t bytes_in_zone = entry.second;
    if (!IsValidZone(zone_id)) continue;

    const ZoneFragState& state = states_[zone_id];
    const double valid_ratio =
        std::min(1.0, static_cast<double>(state.valid_bytes) /
                          static_cast<double>(zone_capacity_bytes_));
    weighted_density += (1.0 - valid_ratio) *
                        (static_cast<double>(bytes_in_zone) /
                         static_cast<double>(file_bytes));
  }

  return static_cast<float>(weighted_density);
}

std::vector<uint64_t> FragmentationStateTable::RankVictimZones(size_t k) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<std::pair<float, uint64_t>> ranked;
  ranked.reserve(states_.size());

  for (const ZoneFragState& state : states_) {
    if (state.valid_bytes == 0) continue;
    const float score = GetRBDNoLock(state);
    if (score <= 0.0f) continue;
    ranked.emplace_back(score, state.zone_id);
  }

  const size_t limit = std::min(k, ranked.size());
  std::partial_sort(ranked.begin(), ranked.begin() + limit, ranked.end(),
                    [](const auto& lhs, const auto& rhs) {
                      if (lhs.first == rhs.first) return lhs.second < rhs.second;
                      return lhs.first > rhs.first;
                    });

  std::vector<uint64_t> zones;
  zones.reserve(limit);
  for (size_t i = 0; i < limit; ++i) {
    zones.push_back(ranked[i].second);
  }
  return zones;
}

ZoneFragState FragmentationStateTable::Snapshot(uint64_t zone_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidZone(zone_id)) return ZoneFragState();
  return states_[zone_id];
}

std::string FragmentationStateTable::DebugString() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "FragmentationStateTable{zone_capacity=" << zone_capacity_bytes_
      << ", zones=" << states_.size() << "}";

  for (const ZoneFragState& state : states_) {
    if (state.valid_bytes == 0 && state.zvdr_ema == 0.0f) continue;
    oss << "\n  zone=" << state.zone_id << " valid=" << state.valid_bytes
        << " prev=" << state.valid_bytes_prev << " zvdr=" << state.zvdr_ema
        << " rbd=" << GetRBDNoLock(state)
        << " class=" << static_cast<int>(state.fragment_class);
  }

  return oss.str();
}

std::string FragmentationStateTable::ExportCsv() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::ostringstream oss;
  oss << "zone_id,valid_bytes,valid_ratio,invalid_bytes,zvdr_ema,rbd,"
         "fragment_class,lifetime_class\n";

  for (const ZoneFragState& state : states_) {
    const uint64_t invalid_bytes =
        zone_capacity_bytes_ > state.valid_bytes
            ? zone_capacity_bytes_ - state.valid_bytes
            : 0;
    const double valid_ratio =
        zone_capacity_bytes_ == 0
            ? 0.0
            : static_cast<double>(state.valid_bytes) /
                  static_cast<double>(zone_capacity_bytes_);
    oss << state.zone_id << "," << state.valid_bytes << "," << valid_ratio
        << "," << invalid_bytes << "," << state.zvdr_ema << ","
        << GetRBDNoLock(state) << ","
        << static_cast<int>(state.fragment_class) << ","
        << static_cast<int>(state.lifetime_class) << "\n";
  }

  return oss.str();
}

std::string FragmentationStateTable::SummaryString(size_t top_k) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::ostringstream oss;
  uint64_t total_valid_bytes = 0;
  uint64_t empty_zones = 0;
  uint64_t active_zones = 0;
  uint64_t class_counts[4] = {0, 0, 0, 0};
  std::vector<std::pair<float, uint64_t>> ranked;
  ranked.reserve(states_.size());

  for (const ZoneFragState& state : states_) {
    total_valid_bytes += state.valid_bytes;
    if (state.valid_bytes > 0) {
      active_zones++;
      if (state.fragment_class < 4) {
        class_counts[state.fragment_class]++;
      }
      const float rbd = GetRBDNoLock(state);
      if (rbd > 0.0f) {
        ranked.emplace_back(rbd, state.zone_id);
      }
    } else {
      empty_zones++;
    }
  }

  const size_t limit = std::min(top_k, ranked.size());
  std::partial_sort(ranked.begin(), ranked.begin() + limit, ranked.end(),
                    [](const auto& lhs, const auto& rhs) {
                      if (lhs.first == rhs.first) return lhs.second < rhs.second;
                      return lhs.first > rhs.first;
                    });

  oss << "zone_capacity_bytes=" << zone_capacity_bytes_ << "\n"
      << "num_zones=" << states_.size() << "\n"
      << "active_zones=" << active_zones << "\n"
      << "empty_zones=" << empty_zones << "\n"
      << "total_valid_bytes=" << total_valid_bytes << "\n"
      << "class_cold_low=" << class_counts[FACO_COLD_LOW] << "\n"
      << "class_cold_high=" << class_counts[FACO_COLD_HIGH] << "\n"
      << "class_hot_low=" << class_counts[FACO_HOT_LOW] << "\n"
      << "class_hot_high=" << class_counts[FACO_HOT_HIGH] << "\n"
      << "top_rbd_zones=zone_id:rbd:valid_bytes:zvdr\n";

  for (size_t i = 0; i < limit; ++i) {
    const ZoneFragState& state = states_[ranked[i].second];
    oss << state.zone_id << ":" << ranked[i].first << ":"
        << state.valid_bytes << ":" << state.zvdr_ema << "\n";
  }

  return oss.str();
}

void FragmentationStateTable::UpdateFragmentClass(ZoneFragState* state) const {
  // Empty zones are free candidates, not fragmented victim candidates.  Keep
  // them in COLD_LOW so class counts describe active data-bearing zones.
  if (zone_capacity_bytes_ == 0 || state->valid_bytes == 0) {
    state->fragment_class = FACO_COLD_LOW;
    return;
  }

  const float invalid_ratio =
      1.0f - std::min(1.0f, static_cast<float>(state->valid_bytes) /
                                static_cast<float>(zone_capacity_bytes_));
  const bool high_invalid = invalid_ratio >= kHighInvalidRatio;
  const bool hot = state->zvdr_ema > kHotZvdr;

  if (hot) {
    state->fragment_class = high_invalid ? FACO_HOT_HIGH : FACO_HOT_LOW;
  } else {
    state->fragment_class = high_invalid ? FACO_COLD_HIGH : FACO_COLD_LOW;
  }
}

float FragmentationStateTable::GetRBDNoLock(
    const ZoneFragState& state) const {
  if (zone_capacity_bytes_ == 0 || state.valid_bytes == 0) {
    return 0.0f;
  }

  const uint64_t invalid_bytes = zone_capacity_bytes_ - state.valid_bytes;
  const double raw_rbd =
      static_cast<double>(invalid_bytes) /
      static_cast<double>(state.valid_bytes + 1);
  const double normalized_space =
      raw_rbd / (1.0 + raw_rbd);  // bound the space term to [0, 1)
  const double score =
      kRbdSpaceWeight * normalized_space + kRbdTrendWeight * state.zvdr_ema;
  if (!std::isfinite(score) || score < 0.0) return 0.0f;
  return static_cast<float>(
      std::min<double>(score, std::numeric_limits<float>::max()));
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
