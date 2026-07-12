// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <string>
#include <vector>

#include "io_zenfs.h"
#include "zbd_zenfs.h"

namespace ROCKSDB_NAMESPACE {

// Indicate what stats info we want.
struct ZenFSSnapshotOptions {
  // Global zoned device stats info
  bool zbd_ = 0;
  // Per zone stats info
  bool zone_ = 0;
  // Get all file->extents & extent->file mappings
  bool zone_file_ = 0;
  bool trigger_report_ = 0;
  bool log_garbage_ = 0;
  bool as_lock_free_as_possible_ = 1;
};

class ZBDSnapshot {
 public:
  uint64_t free_space;
  uint64_t used_space;
  uint64_t reclaimable_space;

 public:
  ZBDSnapshot() = default;
  ZBDSnapshot(const ZBDSnapshot&) = default;
  ZBDSnapshot(ZonedBlockDevice& zbd)
      : free_space(zbd.GetFreeSpace()),
        used_space(zbd.GetUsedSpace()),
        reclaimable_space(zbd.GetReclaimableSpace()) {}
};

class ZoneSnapshot {
 public:
  uint64_t zone_id;
  uint64_t start;
  uint64_t wp;

  uint64_t capacity;
  uint64_t used_capacity;
  uint64_t max_capacity;
  uint32_t lifetime_hint;
  bool is_empty;
  bool is_open;
  bool is_active;
  bool is_full;
  bool is_sealed;
  bool is_busy;
  uint64_t extent_count_total;
  uint64_t first_write_ms;
  uint64_t last_write_seq;
  uint64_t last_invalidate_seq;
  uint64_t reset_count;

 public:
  ZoneSnapshot(const Zone& zone)
      : zone_id(zone.frag_stats_.zone_id),
        start(zone.start_),
        wp(zone.wp_),
        capacity(zone.capacity_),
        used_capacity(zone.used_capacity_),
        max_capacity(zone.max_capacity_),
        lifetime_hint((uint32_t)zone.lifetime_),
        is_empty(zone.wp_ == zone.start_),
        is_open(zone.wp_ != zone.start_ && zone.capacity_ != 0),
        is_active(zone.wp_ != zone.start_ && zone.capacity_ != 0),
        is_full(zone.capacity_ == 0),
        is_sealed(zone.capacity_ == 0),
        is_busy(zone.IsBusy()),
        extent_count_total(zone.frag_stats_.extent_count_total.load()),
        first_write_ms(zone.frag_stats_.first_write_ms.load()),
        last_write_seq(zone.frag_stats_.last_write_seq.load()),
        last_invalidate_seq(zone.frag_stats_.last_invalidate_seq.load()),
        reset_count(zone.frag_stats_.reset_count.load()) {}
};

class ZoneExtentSnapshot {
 public:
  uint64_t start;
  uint64_t length;
  uint64_t zone_start;
  std::string filename;

 public:
  ZoneExtentSnapshot(const ZoneExtent& extent, const std::string fname)
      : start(extent.start_),
        length(extent.length_),
        zone_start(extent.zone_->start_),
        filename(fname) {}
};

class ZoneFileSnapshot {
 public:
  uint64_t file_id;
  std::string filename;
  std::vector<ZoneExtentSnapshot> extents;

 public:
  ZoneFileSnapshot(ZoneFile& file)
      : file_id(file.GetID()), filename(file.GetFilename()) {
    for (const auto* extent : file.GetExtents()) {
      extents.emplace_back(*extent, filename);
    }
  }
};

class ZenFSSnapshot {
 public:
  ZenFSSnapshot() {}

  ZenFSSnapshot& operator=(ZenFSSnapshot&& snapshot) {
    zbd_ = snapshot.zbd_;
    zones_ = std::move(snapshot.zones_);
    zone_files_ = std::move(snapshot.zone_files_);
    extents_ = std::move(snapshot.extents_);
    return *this;
  }

 public:
  ZBDSnapshot zbd_;
  std::vector<ZoneSnapshot> zones_;
  std::vector<ZoneFileSnapshot> zone_files_;
  std::vector<ZoneExtentSnapshot> extents_;
};

}  // namespace ROCKSDB_NAMESPACE
