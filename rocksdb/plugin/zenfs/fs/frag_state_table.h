// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include <cstddef>
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

#ifndef FACO_ENABLE_CFSM
#define FACO_ENABLE_CFSM 1
#endif

/**
 * Coarse fragmentation class used by FACO's CFSM stage.
 *
 * LOW/HIGH describes the current invalid-byte pressure, while COLD/HOT
 * describes whether ZVDR has observed recent validity decay.  The values are
 * stored as uint8_t in ZoneFragState so future metrics export can write compact
 * snapshots without translating enum payloads.
 */
enum FacoFragmentClass : uint8_t {
  FACO_COLD_LOW = 0,
  FACO_COLD_HIGH = 1,
  FACO_HOT_LOW = 2,
  FACO_HOT_HIGH = 3,
};

/**
 * In-memory state for one ZNS zone in the FACO fragmentation model.
 *
 * M1 intentionally keeps this as a small POD-like record.  LACR will fill
 * lifetime_class in M4; CFSM only updates validity, ZVDR, and fragment_class.
 */
struct ZoneFragState {
  uint64_t zone_id = 0;
  uint64_t valid_bytes = 0;
  uint64_t valid_bytes_prev = 0;
  uint64_t last_update_us = 0;
  float zvdr_ema = 0.0f;
  uint8_t fragment_class = FACO_COLD_LOW;
  uint8_t lifetime_class = 0;
};

/**
 * FragmentationStateTable is FACO's cross-layer fragmentation state model.
 *
 * The table tracks one compact state record per zone.  Updates are incremental:
 * append/replay/migration increases valid bytes, deletion/migration decreases
 * valid bytes, and Tick converts the observed decay into an EMA-smoothed ZVDR.
 * It deliberately has no allocation or GC side effects in M1.
 */
class FragmentationStateTable {
 public:
  using FileZoneMapFn =
      std::function<std::vector<std::pair<uint64_t, uint64_t>>(
          const std::string&)>;

  explicit FragmentationStateTable(uint64_t zone_capacity_bytes,
                                   size_t num_zones,
                                   float ema_alpha = 0.3f);

  /** Records newly valid logical bytes for a zone. */
  void OnAppend(uint64_t zone_id, uint64_t bytes);

  /** Records invalidated logical bytes for a zone. */
  void OnDelete(uint64_t zone_id, uint64_t bytes);

  /** Clears all CFSM state after the underlying zone has been reset. */
  void OnZoneReset(uint64_t zone_id);

  /**
   * Converts validity changes since the previous tick into ZVDR samples.
   *
   * now_us must be monotonic in microseconds.  ZenFS calls this from the GC
   * worker; tests can pass deterministic values.
   */
  void Tick(uint64_t now_us);

  /** Returns the EMA-smoothed Zone Validity Decay Rate for one zone. */
  float GetZVDR(uint64_t zone_id) const;

  /** Returns FACO's normalized reclaim benefit density for one zone. */
  float GetRBD(uint64_t zone_id) const;

  /** Injects ZenFS' file-to-zone extent lookup used by FFD queries. */
  void SetFileZoneMapFn(FileZoneMapFn fn);

  /** Computes File Fragmentation Density for filename using the injected map. */
  float GetFFD(const std::string& filename) const;

  /** Ranks zones by RBD, highest score first. */
  std::vector<uint64_t> RankVictimZones(size_t k) const;

  /** Returns a stable copy of one zone's current state. */
  ZoneFragState Snapshot(uint64_t zone_id) const;

  /** Counts active zones currently classified as high-fragmentation. */
  size_t CountHighFragZones() const;

  /** Produces a compact human-readable dump for manual sanity checks. */
  std::string DebugString() const;

  /** Exports every zone record as CSV for experiment artifacts. */
  std::string ExportCsv() const;

  /** Summarizes global CFSM counters and top victim candidates. */
  std::string SummaryString(size_t top_k = 10) const;

  /** Returns the immutable per-zone capacity used by CFSM calculations. */
  uint64_t ZoneCapacityBytes() const { return zone_capacity_bytes_; }

 private:
  bool IsValidZone(uint64_t zone_id) const;
  void UpdateFragmentClass(ZoneFragState* state) const;
  float GetRBDNoLock(const ZoneFragState& state) const;

  const uint64_t zone_capacity_bytes_;
  const float ema_alpha_;
  std::vector<ZoneFragState> states_;
  mutable std::shared_mutex mutex_;
  FileZoneMapFn file_zone_map_fn_;
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
