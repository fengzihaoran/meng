// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "plugin/zenfs/fs/frag_state_table.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "port/stack_trace.h"

namespace ROCKSDB_NAMESPACE {

TEST(FragStateTable, OnAppendUpdatesValidBytes) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1024, /*num_zones=*/4);

  table.OnAppend(/*zone_id=*/1, /*bytes=*/256);

  ZoneFragState state = table.Snapshot(1);
  ASSERT_EQ(state.zone_id, 1);
  ASSERT_EQ(state.valid_bytes, 256);
  ASSERT_EQ(state.valid_bytes_prev, 256);
}

TEST(FragStateTable, OnDeleteDecrementsValidBytes) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1024, /*num_zones=*/4);

  table.OnAppend(/*zone_id=*/2, /*bytes=*/800);
  table.OnDelete(/*zone_id=*/2, /*bytes=*/300);
  ASSERT_EQ(table.Snapshot(2).valid_bytes, 500);

  table.OnDelete(/*zone_id=*/2, /*bytes=*/600);
  ASSERT_EQ(table.Snapshot(2).valid_bytes, 0);
}

TEST(FragStateTable, OnZoneResetClears) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1024, /*num_zones=*/4);

  table.OnAppend(/*zone_id=*/3, /*bytes=*/900);
  table.Tick(/*now_us=*/100);
  table.OnDelete(/*zone_id=*/3, /*bytes=*/400);
  table.Tick(/*now_us=*/200);
  ASSERT_GT(table.GetZVDR(3), 0.0f);

  table.OnZoneReset(/*zone_id=*/3);
  ZoneFragState state = table.Snapshot(3);
  ASSERT_EQ(state.zone_id, 3);
  ASSERT_EQ(state.valid_bytes, 0);
  ASSERT_EQ(state.valid_bytes_prev, 0);
  ASSERT_EQ(state.zvdr_ema, 0.0f);
}

TEST(FragStateTable, ZVDRReflectsDecayRate) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1,
                                /*ema_alpha=*/1.0f);

  table.OnAppend(/*zone_id=*/0, /*bytes=*/1000);
  table.Tick(/*now_us=*/1000);
  table.OnDelete(/*zone_id=*/0, /*bytes=*/500);
  table.Tick(/*now_us=*/101000);

  const float expected = 500.0f / (100000.0f * 1000.0f);
  ASSERT_NEAR(table.GetZVDR(0), expected, 1e-12f);
}

TEST(FragStateTable, ZVDRSmoothedByEMA) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1,
                                /*ema_alpha=*/0.5f);

  table.OnAppend(/*zone_id=*/0, /*bytes=*/1000);
  table.Tick(/*now_us=*/1000);
  table.OnDelete(/*zone_id=*/0, /*bytes=*/500);
  table.Tick(/*now_us=*/101000);

  const float inst = 500.0f / (100000.0f * 1000.0f);
  ASSERT_NEAR(table.GetZVDR(0), inst * 0.5f, 1e-12f);

  table.Tick(/*now_us=*/201000);
  ASSERT_NEAR(table.GetZVDR(0), inst * 0.25f, 1e-12f);
}

TEST(FragStateTable, ZVDRCapturesSameTimestampDelete) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/1,
                                /*ema_alpha=*/1.0f);

  table.OnAppend(/*zone_id=*/0, /*bytes=*/1000);
  table.Tick(/*now_us=*/1000);
  table.OnDelete(/*zone_id=*/0, /*bytes=*/500);
  table.Tick(/*now_us=*/1000);

  // The table uses a 1000 us minimum sampling window when callbacks happen in
  // the same microsecond, so the decay signal remains observable.
  ASSERT_NEAR(table.GetZVDR(0), 500.0f / (1000.0f * 1000.0f), 1e-12f);
  ASSERT_EQ(table.Snapshot(0).fragment_class, FACO_HOT_HIGH);
}

TEST(FragStateTable, RBDOrderingMatchesExpected) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/3);

  table.OnAppend(/*zone_id=*/0, /*bytes=*/900);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/500);
  table.OnAppend(/*zone_id=*/2, /*bytes=*/750);

  ASSERT_GT(table.GetRBD(1), table.GetRBD(2));
  ASSERT_GT(table.GetRBD(2), table.GetRBD(0));

  std::vector<uint64_t> ranked = table.RankVictimZones(/*k=*/2);
  ASSERT_EQ(ranked.size(), 2);
  ASSERT_EQ(ranked[0], 1);
  ASSERT_EQ(ranked[1], 2);
}

TEST(FragStateTable, FFDComputeForMultiZoneFile) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);
  table.OnAppend(/*zone_id=*/0, /*bytes=*/800);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/200);
  table.SetFileZoneMapFn(
      [](const std::string& filename)
          -> std::vector<std::pair<uint64_t, uint64_t>> {
        if (filename != "/db/000123.sst") return {};
        return {{0, 100}, {1, 300}};
      });

  // FFD = 20% * 100/400 + 80% * 300/400 = 0.65.
  ASSERT_NEAR(table.GetFFD("/db/000123.sst"), 0.65f, 1e-6f);
}

TEST(FragStateTable, ConcurrentAppendDeleteThreadSafety) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1ull << 30,
                                /*num_zones=*/4);
  static constexpr int kThreads = 4;
  static constexpr int kIterations = 10000;
  std::vector<std::thread> threads;

  for (int tid = 0; tid < kThreads; ++tid) {
    threads.emplace_back([&table, tid] {
      for (int i = 0; i < kIterations; ++i) {
        table.OnAppend(tid, 2);
        table.OnDelete(tid, 1);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (int tid = 0; tid < kThreads; ++tid) {
    ASSERT_EQ(table.Snapshot(tid).valid_bytes, kIterations);
  }
}

TEST(FragStateTable, LargeScale1000ZonesPerf) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1ull << 30,
                                /*num_zones=*/1000);
  constexpr int kIterations = 10000;

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < kIterations; ++i) {
    table.OnAppend(i % 1000, 4096);
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double avg_us =
      std::chrono::duration<double, std::micro>(elapsed).count() / kIterations;

  // Keep the guard generous for debug builds while still catching accidental
  // full-table scans in the hot update path.
  ASSERT_LT(avg_us, 50.0);
  ASSERT_EQ(table.RankVictimZones(/*k=*/10).size(), 10);
}

TEST(FragStateTable, ExportCsvAndSummaryContainState) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);

  table.OnAppend(/*zone_id=*/0, /*bytes=*/900);
  table.OnAppend(/*zone_id=*/1, /*bytes=*/500);
  table.OnDelete(/*zone_id=*/1, /*bytes=*/250);
  table.Tick(/*now_us=*/1000);

  const std::string csv = table.ExportCsv();
  ASSERT_NE(csv.find("zone_id,valid_bytes,valid_ratio"), std::string::npos);
  ASSERT_NE(csv.find("0,900,"), std::string::npos);
  ASSERT_NE(csv.find("1,250,"), std::string::npos);

  const std::string summary = table.SummaryString(/*top_k=*/1);
  ASSERT_NE(summary.find("active_zones=2"), std::string::npos);
  ASSERT_NE(summary.find("empty_zones=0"), std::string::npos);
  ASSERT_NE(summary.find("top_rbd_zones=zone_id:rbd:valid_bytes:zvdr"),
            std::string::npos);
}

TEST(FragStateTable, EmptyZonesAreNotClassifiedAsHighFragmentation) {
  FragmentationStateTable table(/*zone_capacity_bytes=*/1000, /*num_zones=*/2);

  table.OnAppend(/*zone_id=*/0, /*bytes=*/700);

  ASSERT_EQ(table.Snapshot(1).fragment_class, FACO_COLD_LOW);
  const std::string summary = table.SummaryString(/*top_k=*/2);
  ASSERT_NE(summary.find("active_zones=1"), std::string::npos);
  ASSERT_NE(summary.find("empty_zones=1"), std::string::npos);
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
