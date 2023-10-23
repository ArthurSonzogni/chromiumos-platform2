// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_usage_policy.h"

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "vm_concierge/vmm_swap_policy.pb.h"
#include "vm_tools/concierge/byte_unit.h"

namespace vm_tools::concierge {

namespace {
class VmmSwapUsagePolicyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    history_file_path_ = temp_dir_.GetPath().Append("usage_history");
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath history_file_path_;
};
}  // namespace

TEST_F(VmmSwapUsagePolicyTest, PredictDuration) {
  VmmSwapUsagePolicy policy(history_file_path_);

  EXPECT_TRUE(policy.PredictDuration().is_zero());
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationJustLogLongTimeAgo) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(29));
  policy.OnDisabled(now - base::Days(28) - base::Seconds(1));

  EXPECT_TRUE(policy.PredictDuration(now).is_zero());
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationEnabledFullTime) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(29));

  EXPECT_EQ(policy.PredictDuration(now), base::Days(28 + 21 + 14 + 7) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationWithMissingEnabledRecord) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(29));
  policy.OnDisabled(now - base::Days(29) + base::Minutes(50));
  // This enabled record is skipped.
  policy.OnEnabled(now - base::Days(29) + base::Minutes(30));

  EXPECT_EQ(policy.PredictDuration(now), base::Days(28 + 21 + 14 + 7) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLessThan1WeekDataWhileDisabled) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(7) + base::Hours(1));
  policy.OnDisabled(now - base::Days(7) + base::Hours(10));

  policy.OnEnabled(now - base::Days(6));
  policy.OnDisabled(now - base::Days(6) + base::Hours(1));

  // The latest enabled duration * 2
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(2));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLessThan1WeekDataWhileEnabled) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(6));
  policy.OnDisabled(now - base::Days(6) + base::Hours(1));

  policy.OnEnabled(now - base::Minutes(10));

  // The latest enabled duration * 2
  EXPECT_EQ(policy.PredictDuration(now), base::Minutes(20));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationJust1WeekData) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(7));
  policy.OnDisabled(now - base::Days(7) + base::Hours(10));

  policy.OnEnabled(now - base::Days(6));

  // The latest enabled duration
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(10));
}

TEST_F(VmmSwapUsagePolicyTest,
       PredictDurationLessThan1WeekDataWhileMultipleEnabled) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Minutes(50));
  policy.OnDisabled(now - base::Minutes(30));
  policy.OnEnabled(now - base::Minutes(5));

  // The latest enabled duration in 1 hour * 2.
  EXPECT_EQ(policy.PredictDuration(now), base::Minutes(40));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLessThan2WeekData) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(10));
  policy.OnDisabled(now - base::Days(8));
  // Enabled record across the point 1 week ago.
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(1));
  policy.OnEnabled(now - base::Minutes(30));

  EXPECT_EQ(policy.PredictDuration(now), base::Hours(1));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLessThan3WeekData) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(4));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 4 + 6 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(5));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLessThan4WeekData) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(21) - base::Hours(2));
  policy.OnDisabled(now - base::Days(21) + base::Hours(2));
  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(4));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 2 + 4 + 6 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(4));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationFullData) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(16));
  policy.OnEnabled(now - base::Days(21) - base::Hours(2));
  policy.OnDisabled(now - base::Days(21) + base::Hours(2));
  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(4));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 16 + 2 + 4 + 6 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(7));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationFullDataWithEmptyWeeks) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(16));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of 16 + 0 + 0 + 0 hours.
  EXPECT_EQ(policy.PredictDuration(now), base::Hours(4));
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLong2WeeksData4Weeks) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(21) + base::Hours(3));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of (7days + 3hours) + 3hours + 0 + 6hours.
  EXPECT_EQ(policy.PredictDuration(now), (base::Days(7) + base::Hours(12)) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLong3WeeksData4Weeks) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(3));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of (14days + 3hours) + (7days+3hours) + 3hours + 6hours.
  EXPECT_EQ(policy.PredictDuration(now),
            (base::Days(21) + base::Hours(15)) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLong4WeeksData4Weeks) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(3));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of (21days + 3hours) + (14days+3hours) + (7days+3hours) + 3hours.
  EXPECT_EQ(policy.PredictDuration(now),
            (base::Days(42) + base::Hours(12)) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLongData3Weeks) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(3));
  policy.OnEnabled(now - base::Days(21) - base::Hours(2));
  policy.OnDisabled(now - base::Days(14) + base::Hours(3));
  policy.OnEnabled(now - base::Days(7) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(6));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of  3hours + (7days + 3hours) + 3hours + 6hours.
  EXPECT_EQ(policy.PredictDuration(now), (base::Days(7) + base::Hours(15)) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, PredictDurationLongData2Weeks) {
  VmmSwapUsagePolicy policy(history_file_path_);
  base::Time now = base::Time::Now();

  policy.OnEnabled(now - base::Days(28) - base::Hours(2));
  policy.OnDisabled(now - base::Days(28) + base::Hours(3));
  policy.OnEnabled(now - base::Days(14) - base::Hours(2));
  policy.OnDisabled(now - base::Days(7) + base::Hours(3));
  policy.OnEnabled(now - base::Minutes(30));

  // Average of  3hours + 0 + (7days + 3hours) + 3hours.
  EXPECT_EQ(policy.PredictDuration(now), (base::Days(7) + base::Hours(9)) / 4);
}

TEST_F(VmmSwapUsagePolicyTest, Init) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy policy(history_file_path_);

  EXPECT_TRUE(policy.Init(now));

  // Creates history file
  EXPECT_TRUE(base::PathExists(history_file_path_));
  int64_t file_size = -1;
  ASSERT_TRUE(base::GetFileSize(history_file_path_, &file_size));
  EXPECT_EQ(file_size, 0);
}

TEST_F(VmmSwapUsagePolicyTest, InitTwice) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy policy(history_file_path_);

  EXPECT_TRUE(policy.Init(now));
  EXPECT_FALSE(policy.Init(now));
}

TEST_F(VmmSwapUsagePolicyTest, InitIfFileNotExist) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy policy(history_file_path_);

  ASSERT_TRUE(policy.Init(now));

  // The history is empty.
  EXPECT_EQ(policy.PredictDuration(now), base::TimeDelta());
  policy.OnEnabled(now - base::Days(8));
  EXPECT_EQ(policy.PredictDuration(now), base::Days(7));
}

TEST_F(VmmSwapUsagePolicyTest, InitIfFileExists) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy policy(history_file_path_);

  // Create file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  EXPECT_TRUE(policy.Init(now));

  // The history is empty.
  EXPECT_EQ(policy.PredictDuration(now), base::TimeDelta());
  policy.OnEnabled(now - base::Days(8));
  EXPECT_EQ(policy.PredictDuration(now), base::Days(7));
}

TEST_F(VmmSwapUsagePolicyTest, InitIfFileIsBroken) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy policy(history_file_path_);

  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  ASSERT_TRUE(history_file.Write(0, "invalid_data", 12));
  EXPECT_FALSE(policy.Init(now));

  // The history is empty.
  EXPECT_EQ(policy.PredictDuration(now), base::TimeDelta());
  policy.OnEnabled(now - base::Days(8));
  EXPECT_EQ(policy.PredictDuration(now), base::Days(7));
}

TEST_F(VmmSwapUsagePolicyTest, InitIfFileIsTooLong) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy policy(history_file_path_);

  UsageHistoryEntryContainer container;
  while (container.ByteSizeLong() <= 5 * KiB(4)) {
    auto entry = container.add_entries();
    entry->set_start_time_us(now.ToDeltaSinceWindowsEpoch().InMicroseconds());
    // 1 hour
    entry->set_duration_us(3600 * 1000 & 1000);
    entry->set_is_shutdown(false);
    now += base::Hours(1);
  }
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  ASSERT_TRUE(
      container.SerializeToFileDescriptor(history_file.GetPlatformFile()));

  EXPECT_FALSE(policy.Init(now));
  // The history is empty.
  EXPECT_EQ(policy.PredictDuration(now), base::TimeDelta());
  policy.OnEnabled(now - base::Days(8));
  EXPECT_EQ(policy.PredictDuration(now), base::Days(7));
}

TEST_F(VmmSwapUsagePolicyTest, OnDisabledWriteEntriesToFile) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));
  // 1 day
  before_policy.OnEnabled(now - base::Days(28));
  before_policy.OnDisabled(now - base::Days(27));
  // 2 days
  before_policy.OnEnabled(now - base::Days(21) - base::Hours(1));
  before_policy.OnDisabled(now - base::Days(21) - base::Minutes(30));
  before_policy.OnEnabled(now - base::Days(21) - base::Minutes(10));
  before_policy.OnDisabled(now - base::Days(19));
  // 3 days
  before_policy.OnEnabled(now - base::Days(14));
  before_policy.OnDisabled(now - base::Days(11));
  // 6 days
  before_policy.OnEnabled(now - base::Days(7));
  before_policy.OnDisabled(now - base::Days(1));
  ASSERT_EQ(before_policy.PredictDuration(now), base::Days(3));

  EXPECT_TRUE(after_policy.Init(now));

  EXPECT_EQ(after_policy.PredictDuration(now), base::Days(3));
}

TEST_F(VmmSwapUsagePolicyTest, OnDestroyWriteEntriesToFile) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));
  // 1 day
  before_policy.OnEnabled(now - base::Days(14));
  before_policy.OnDisabled(now - base::Days(13));
  // 2 days
  before_policy.OnEnabled(now - base::Days(7));
  before_policy.OnDestroy(now - base::Days(5));
  ASSERT_EQ(before_policy.PredictDuration(now), base::Days(3) / 2);

  EXPECT_TRUE(after_policy.Init(now));

  EXPECT_EQ(after_policy.PredictDuration(now), base::Days(3) / 2);
}

TEST_F(VmmSwapUsagePolicyTest, OnDestroyWithoutDisable) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));
  // 13 days + 6 days
  before_policy.OnEnabled(now - base::Days(14));
  before_policy.OnDestroy(now - base::Days(1));
  ASSERT_EQ(before_policy.PredictDuration(now), base::Days(19) / 2);

  EXPECT_TRUE(after_policy.Init(now));

  EXPECT_EQ(after_policy.PredictDuration(now), base::Days(19) / 2);
}

TEST_F(VmmSwapUsagePolicyTest, OnDisableLatestEnableWithin1hour) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));
  // 1 day
  before_policy.OnEnabled(now - base::Days(14));
  before_policy.OnDisabled(now - base::Days(13));
  // Add entry at end of week, but doesn't contribute to predicted duration
  before_policy.OnEnabled(now - base::Days(7) - base::Hours(1));
  before_policy.OnDisabled(now - base::Days(7) - base::Minutes(30));
  // This enable record is not in the ring buffer.
  before_policy.OnEnabled(now - base::Days(7) - base::Minutes(10));
  // Enabled close to previous entry, so pesimestically becomes 6 days
  before_policy.OnDisabled(now - base::Days(1));
  ASSERT_EQ(before_policy.PredictDuration(now), base::Days(7) / 2);

  EXPECT_TRUE(after_policy.Init(now));

  EXPECT_EQ(after_policy.PredictDuration(now), base::Days(7) / 2);
}

TEST_F(VmmSwapUsagePolicyTest, InitMultipleShutdownRecordAreIgnored) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));
  // 2 days
  before_policy.OnEnabled(now - base::Days(14));
  before_policy.OnDestroy(now - base::Days(12));
  before_policy.OnDisabled(now - base::Days(11));
  // 1 day
  before_policy.OnEnabled(now - base::Days(7));
  before_policy.OnDestroy(now - base::Days(6));
  before_policy.OnDisabled(now - base::Days(2));
  ASSERT_EQ(before_policy.PredictDuration(now), base::Days(3) / 2);

  EXPECT_TRUE(after_policy.Init(now));

  EXPECT_EQ(after_policy.PredictDuration(now), base::Days(3) / 2);
}

TEST_F(VmmSwapUsagePolicyTest, OnDisabledRotateHistoryFile) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));

  int64_t before_file_size = -1;
  for (int i = 0;
       before_file_size < 5 * 4096 - VmmSwapUsagePolicy::kMaxEntrySize; i++) {
    before_policy.OnEnabled(now);
    now += base::Hours(1);
    before_policy.OnDisabled(now);
    if (i >= 5 * 4096 / 25) {
      ASSERT_TRUE(base::GetFileSize(history_file_path_, &before_file_size));
    }
  }
  before_policy.OnEnabled(now);
  now += base::Hours(1);
  before_policy.OnDisabled(now);
  int64_t after_file_size = -1;
  ASSERT_TRUE(base::GetFileSize(history_file_path_, &after_file_size));
  EXPECT_LT(after_file_size, before_file_size);

  ASSERT_EQ(before_policy.PredictDuration(now), base::Hours(1));
  EXPECT_TRUE(after_policy.Init(now));
  // The file content is valid after rotation.
  EXPECT_EQ(after_policy.PredictDuration(now), base::Hours(1));
}

TEST_F(VmmSwapUsagePolicyTest, OnDestroyRotateHistoryFile) {
  base::Time now = base::Time::Now();
  VmmSwapUsagePolicy before_policy(history_file_path_);
  VmmSwapUsagePolicy after_policy(history_file_path_);
  ASSERT_TRUE(before_policy.Init(now));

  int64_t before_file_size = -1;
  for (int i = 0; before_file_size < 5 * 4096 - 25; i++) {
    before_policy.OnEnabled(now);
    now += base::Hours(1);
    before_policy.OnDisabled(now);
    if (i >= 5 * 4096 / 25) {
      ASSERT_TRUE(base::GetFileSize(history_file_path_, &before_file_size));
    }
  }
  before_policy.OnEnabled(now);
  now += base::Hours(1);
  before_policy.OnDestroy(now);
  int64_t after_file_size = -1;
  ASSERT_TRUE(base::GetFileSize(history_file_path_, &after_file_size));
  EXPECT_LT(after_file_size, before_file_size);

  ASSERT_EQ(before_policy.PredictDuration(now), base::Hours(1));
  EXPECT_TRUE(after_policy.Init(now));
  // The file content is valid after rotation.
  EXPECT_EQ(after_policy.PredictDuration(now), base::Hours(1));
}

TEST_F(VmmSwapUsagePolicyTest, MaxEntrySize) {
  UsageHistoryEntryContainer container;
  UsageHistoryEntry* new_entry = container.add_entries();
  // -1 gives the max varint length.
  new_entry->set_start_time_us(-1);
  new_entry->set_duration_us(-1);
  new_entry->set_is_shutdown(true);

  EXPECT_EQ(container.ByteSizeLong(), VmmSwapUsagePolicy::kMaxEntrySize);
}

}  // namespace vm_tools::concierge
