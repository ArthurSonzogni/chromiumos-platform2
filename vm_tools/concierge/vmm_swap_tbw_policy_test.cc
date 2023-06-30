// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_tbw_policy.h"

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
bool WriteOldFormatEntry(base::File& file,
                         uint64_t bytes_written,
                         base::Time time) {
  TbwHistoryEntry entry;
  uint8_t message_size_buf[1];
  entry.set_time_us(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
  entry.set_size(bytes_written);
  // TbwHistoryEntry message is less than 127 bytes. The MSB is reserved for
  // future extensibility.
  if (entry.ByteSizeLong() > 127) {
    return false;
  }
  message_size_buf[0] = entry.ByteSizeLong();
  if (!base::WriteFileDescriptor(file.GetPlatformFile(), message_size_buf)) {
    return false;
  }
  return entry.SerializeToFileDescriptor(file.GetPlatformFile());
}
class VmmSwapTbwPolicyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    history_file_path_ = temp_dir_.GetPath().Append("tbw_history2");
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath history_file_path_;
};
}  // namespace

TEST_F(VmmSwapTbwPolicyTest, CanSwapOut) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutWithin1dayTarget) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  policy.Record(399);

  EXPECT_TRUE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds1dayTarget) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  policy.Record(400);

  EXPECT_FALSE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds1dayTargetWithMultiRecords) {
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  // Buffer size is 28 but they are merged within 1 day.
  for (int i = 0; i < 100; i++) {
    policy.Record(4);
  }

  EXPECT_FALSE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds1dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  policy.Record(400, now - base::Days(1));

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 7; i++) {
    policy.Record(200, now - base::Days(6 - i));
  }

  EXPECT_FALSE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutNotExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 6; i++) {
    policy.Record(200, now - base::Days(6 - i));
  }
  policy.Record(199, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 7; i++) {
    policy.Record(200, now - base::Days(7 - i));
  }

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(100, now - base::Days(27 - i));
  }

  EXPECT_FALSE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutNotExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 27; i++) {
    policy.Record(100, now - base::Days(27 - i));
  }
  policy.Record(99, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(100, now - base::Days(28 - i));
  }

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutIgnoreRotatedObsoleteData) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(400, now - base::Days(56 - i));
  }
  policy.Record(399, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, Init) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.Init(history_file_path_, now));

  // Creates history file
  EXPECT_TRUE(base::PathExists(history_file_path_));
}

TEST_F(VmmSwapTbwPolicyTest, InitTwice) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.Init(history_file_path_, now));
  EXPECT_FALSE(policy.Init(history_file_path_, now));
}

TEST_F(VmmSwapTbwPolicyTest, InitIfFileNotExist) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.Init(history_file_path_, now));

  // By default it is not allowed to swap out for 1 day.
  EXPECT_FALSE(policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, InitIfFileExists) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  // Create file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  EXPECT_TRUE(policy.Init(history_file_path_, now));

  // The history is empty.
  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, InitIfFileIsBroken) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy;
  policy.SetTargetTbwPerDay(100);

  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  ASSERT_TRUE(history_file.Write(0, "invalid_data", 12));
  EXPECT_FALSE(policy.Init(history_file_path_, now));

  // The pessimistic history does not allow to swap out for 1 day.
  EXPECT_FALSE(policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, InititialHistoryFile) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy before_policy;
  VmmSwapTbwPolicy after_policy;
  before_policy.SetTargetTbwPerDay(100);
  after_policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(before_policy.Init(history_file_path_, now));
  EXPECT_TRUE(after_policy.Init(history_file_path_, now));

  // The initialized history from policy1 is written into the history file.
  EXPECT_FALSE(after_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, RecordWriteEntriesToFile) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy before_policy;
  VmmSwapTbwPolicy after_policy;
  before_policy.SetTargetTbwPerDay(100);
  after_policy.SetTargetTbwPerDay(100);
  // Create empty file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  EXPECT_TRUE(before_policy.Init(history_file_path_, now));

  for (int i = 0; i < 7; i++) {
    before_policy.Record(200, now + base::Days(i));
  }
  now = now + base::Days(6);
  EXPECT_TRUE(after_policy.Init(history_file_path_, now));

  EXPECT_FALSE(after_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, RecordRotateHistoryFile) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy before_policy;
  VmmSwapTbwPolicy after_policy;
  // Create empty file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  EXPECT_TRUE(before_policy.Init(history_file_path_, now));

  int64_t before_file_size = -1;
  int64_t count = 0;
  for (; before_file_size < 4096 - VmmSwapTbwPolicy::kMaxEntrySize; count++) {
    before_policy.Record(4, now);
    if (count >= 4096 / VmmSwapTbwPolicy::kMaxEntrySize) {
      ASSERT_TRUE(base::GetFileSize(history_file_path_, &before_file_size));
    }
  }
  before_policy.Record(1, now);
  int64_t after_file_size = -1;
  ASSERT_TRUE(base::GetFileSize(history_file_path_, &after_file_size));
  EXPECT_LT(after_file_size, before_file_size);

  before_policy.SetTargetTbwPerDay(count);
  after_policy.SetTargetTbwPerDay(count);
  EXPECT_FALSE(before_policy.CanSwapOut(now));
  EXPECT_TRUE(before_policy.CanSwapOut(now + base::Days(1)));
  EXPECT_TRUE(after_policy.Init(history_file_path_, now));
  EXPECT_FALSE(after_policy.CanSwapOut(now));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));

  // Less than page size.
  EXPECT_LE(history_file.GetLength(), KiB(4));
}

TEST_F(VmmSwapTbwPolicyTest, InitMigratesOldFormatIntoNewFormat) {
  base::Time now = base::Time::Now();
  base::FilePath old_file_path =
      history_file_path_.DirName().Append("tbw_history");
  // Create old file
  base::File old_history_file = base::File(
      old_file_path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  for (int i = 0; i < 7; i++) {
    ASSERT_TRUE(
        WriteOldFormatEntry(old_history_file, 200, now + base::Days(i)));
  }
  now += base::Days(6);

  VmmSwapTbwPolicy before_policy;
  VmmSwapTbwPolicy after_policy;
  before_policy.SetTargetTbwPerDay(100);
  after_policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(before_policy.Init(history_file_path_, now));
  // The old file is removed
  EXPECT_FALSE(base::PathExists(old_file_path));
  EXPECT_FALSE(
      before_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(before_policy.CanSwapOut(now + base::Days(1)));
  now += base::Days(15);

  // Appending entries to the migrated file should not break the file.
  before_policy.Record(400, now);
  EXPECT_FALSE(
      before_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(before_policy.CanSwapOut(now + base::Days(1)));

  EXPECT_TRUE(after_policy.Init(history_file_path_, now));
  EXPECT_FALSE(after_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));
}

}  // namespace vm_tools::concierge
