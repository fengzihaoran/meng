// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "db/faco_lacr_state.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "port/stack_trace.h"

namespace ROCKSDB_NAMESPACE {

namespace {

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

CompactionJobInfo MakeCompaction(int job_id, const std::string& input_file,
                                 uint64_t input_bytes,
                                 uint64_t output_bytes) {
  CompactionJobInfo info;
  info.job_id = job_id;
  info.base_input_level = 1;
  info.output_level = 2;
  info.input_files.push_back(input_file);
  info.output_files.push_back("/000099.sst");
  info.stats.total_input_bytes = input_bytes;
  info.stats.total_output_bytes = output_bytes;
  return info;
}

#endif  // FACO_ENABLE_LACR

}  // namespace

#if FACO_ENABLE_LACR

TEST(FacoLacrState, ActiveCompactionFileMapsToZone) {
  ScopedEnv enabled("FACO_LACR_ENABLE", "1");
  FacoLacrState& state = GetFacoLacrState();
  state.ResetForTests();
  state.SetFileZoneMapFn([](const std::string& file) {
    if (file == "/000001.sst") {
      return std::vector<std::pair<uint64_t, uint64_t>>{{7, 4096}};
    }
    return std::vector<std::pair<uint64_t, uint64_t>>{};
  });

  state.MarkCompactionBegin(
      MakeCompaction(/*job_id=*/11, "/000001.sst", 4096, 0));

  ASSERT_TRUE(state.IsFileInActiveCompaction("/000001.sst"));
  ASSERT_EQ(state.ZoneActiveCompactionBytes(7), 4096);
  ASSERT_GT(state.ZoneCompactionScore(7), 0);
  ASSERT_NE(state.ExportTraceCsv().find("touched_zones"), std::string::npos);
  state.ResetForTests();
}

TEST(FacoLacrState, CompletedCompactionRecordsRecentInvalidation) {
  ScopedEnv enabled("FACO_LACR_ENABLE", "1");
  FacoLacrState& state = GetFacoLacrState();
  state.ResetForTests();
  state.SetFileZoneMapFn([](const std::string& file) {
    if (file == "/000002.sst") {
      return std::vector<std::pair<uint64_t, uint64_t>>{{3, 1000}};
    }
    return std::vector<std::pair<uint64_t, uint64_t>>{};
  });

  CompactionJobInfo info =
      MakeCompaction(/*job_id=*/12, "/000002.sst", 1000, 400);
  state.MarkCompactionBegin(info);
  state.MarkCompactionEnd(info);

  ASSERT_EQ(state.ActiveCompactionFiles(), 0);
  ASSERT_EQ(state.ZoneActiveCompactionBytes(3), 0);
  ASSERT_GT(state.ZoneRecentInvalidationBytes(3), 0);
  ASSERT_TRUE(state.WasZoneTouchedByCompaction(3));
  state.ResetForTests();
}

TEST(FacoLacrState, BeginEndUpdatesAreThreadSafe) {
  ScopedEnv enabled("FACO_LACR_ENABLE", "1");
  FacoLacrState& state = GetFacoLacrState();
  state.ResetForTests();
  state.SetFileZoneMapFn([](const std::string& file) {
    if (file.find(".sst") != std::string::npos) {
      return std::vector<std::pair<uint64_t, uint64_t>>{{5, 1024}};
    }
    return std::vector<std::pair<uint64_t, uint64_t>>{};
  });

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([i, &state] {
      CompactionJobInfo info = MakeCompaction(
          100 + i, "/thread-" + std::to_string(i) + ".sst", 1024, 512);
      state.MarkCompactionBegin(info);
      state.MarkCompactionEnd(info);
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  ASSERT_EQ(state.ActiveCompactionFiles(), 0);
  ASSERT_NE(state.DebugString().find("active_jobs=0"), std::string::npos);
  ASSERT_GT(state.ZoneRecentInvalidationBytes(5), 0);
  state.ResetForTests();
}

#endif  // FACO_ENABLE_LACR

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
