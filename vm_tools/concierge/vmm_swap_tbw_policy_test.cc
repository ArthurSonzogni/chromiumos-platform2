// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_tbw_policy.h"

#include <memory>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <base/timer/mock_timer.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "vm_concierge/vmm_swap_policy.pb.h"
#include "vm_tools/concierge/byte_unit.h"

using ::testing::_;

namespace vm_tools::concierge {

namespace {
static constexpr char kMetricsTotalBytesWrittenInAWeek[] =
    "Memory.VmmSwap.TotalBytesWrittenInAWeek";

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

  raw_ref<MetricsLibraryInterface> GetMetricsRef() {
    return raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir temp_dir_;
  base::FilePath history_file_path_;
  std::unique_ptr<MetricsLibraryMock> metrics_ =
      std::make_unique<MetricsLibraryMock>();
  std::unique_ptr<base::MockRepeatingTimer> report_timer_ =
      std::make_unique<base::MockRepeatingTimer>();
};
}  // namespace

TEST_F(VmmSwapTbwPolicyTest, CanSwapOut) {
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutWithin1dayTarget) {
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  policy.Record(399);

  EXPECT_TRUE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds1dayTarget) {
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  policy.Record(400);

  EXPECT_FALSE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds1dayTargetWithMultiRecords) {
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  // Buffer size is 28 but they are merged within 1 day.
  for (int i = 0; i < 100; i++) {
    policy.Record(4);
  }

  EXPECT_FALSE(policy.CanSwapOut());
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds1dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  policy.Record(400, now - base::Days(1));

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 7; i++) {
    policy.Record(200, now - base::Days(6 - i));
  }

  EXPECT_FALSE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutNotExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 6; i++) {
    policy.Record(200, now - base::Days(6 - i));
  }
  policy.Record(199, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds7dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 7; i++) {
    policy.Record(200, now - base::Days(7 - i));
  }

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(100, now - base::Days(27 - i));
  }

  EXPECT_FALSE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutNotExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 27; i++) {
    policy.Record(100, now - base::Days(27 - i));
  }
  policy.Record(99, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutAfterExceeds28dayTarget) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(100, now - base::Days(28 - i));
  }

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, CanSwapOutIgnoreRotatedObsoleteData) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  for (int i = 0; i < 28; i++) {
    policy.Record(400, now - base::Days(56 - i));
  }
  policy.Record(399, now);

  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, Init) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.Init(now));

  // Creates history file
  EXPECT_TRUE(base::PathExists(history_file_path_));
}

TEST_F(VmmSwapTbwPolicyTest, InitTwice) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.Init(now));
  EXPECT_FALSE(policy.Init(now));
}

TEST_F(VmmSwapTbwPolicyTest, InitIfFileNotExist) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(policy.Init(now));

  // By default it is not allowed to swap out for 1 day.
  EXPECT_FALSE(policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, InitIfFileExists) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  // Create file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  EXPECT_TRUE(policy.Init(now));

  // The history is empty.
  EXPECT_TRUE(policy.CanSwapOut(now));
}

TEST_F(VmmSwapTbwPolicyTest, InitIfFileIsBroken) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(100);

  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(history_file.IsValid());
  ASSERT_TRUE(history_file.Write(0, "invalid_data", 12));
  EXPECT_FALSE(policy.Init(now));

  // The pessimistic history does not allow to swap out for 1 day.
  EXPECT_FALSE(policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, InititialHistoryFile) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy before_policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  VmmSwapTbwPolicy after_policy =
      VmmSwapTbwPolicy(GetMetricsRef(), history_file_path_,
                       std::make_unique<base::MockRepeatingTimer>());
  before_policy.SetTargetTbwPerDay(100);
  after_policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(before_policy.Init(now));
  EXPECT_TRUE(after_policy.Init(now));

  // The initialized history from policy1 is written into the history file.
  EXPECT_FALSE(after_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, RecordWriteEntriesToFile) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy before_policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  VmmSwapTbwPolicy after_policy =
      VmmSwapTbwPolicy(GetMetricsRef(), history_file_path_,
                       std::make_unique<base::MockRepeatingTimer>());
  before_policy.SetTargetTbwPerDay(100);
  after_policy.SetTargetTbwPerDay(100);
  // Create empty file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  EXPECT_TRUE(before_policy.Init(now));

  for (int i = 0; i < 7; i++) {
    before_policy.Record(200, now + base::Days(i));
  }
  now = now + base::Days(6);
  EXPECT_TRUE(after_policy.Init(now));

  EXPECT_FALSE(after_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, RecordRotateHistoryFile) {
  base::Time now = base::Time::Now();
  VmmSwapTbwPolicy before_policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  VmmSwapTbwPolicy after_policy =
      VmmSwapTbwPolicy(GetMetricsRef(), history_file_path_,
                       std::make_unique<base::MockRepeatingTimer>());
  int target = 60 * 24;
  before_policy.SetTargetTbwPerDay(target);
  after_policy.SetTargetTbwPerDay(target);
  // Create empty file
  base::File history_file = base::File(
      history_file_path_, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  EXPECT_TRUE(before_policy.Init(now));

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
  EXPECT_TRUE(after_policy.Init(now));
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

  VmmSwapTbwPolicy before_policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  VmmSwapTbwPolicy after_policy =
      VmmSwapTbwPolicy(GetMetricsRef(), history_file_path_,
                       std::make_unique<base::MockRepeatingTimer>());
  before_policy.SetTargetTbwPerDay(100);
  after_policy.SetTargetTbwPerDay(100);

  EXPECT_TRUE(before_policy.Init(now));
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

  EXPECT_TRUE(after_policy.Init(now));
  EXPECT_FALSE(after_policy.CanSwapOut(now + base::Days(1) - base::Seconds(1)));
  EXPECT_TRUE(after_policy.CanSwapOut(now + base::Days(1)));
}

TEST_F(VmmSwapTbwPolicyTest, InitNotTriggerReportTimer) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(MiB(500));

  ASSERT_TRUE(policy.Init());

  EXPECT_FALSE(report_timer->IsRunning());
}

TEST_F(VmmSwapTbwPolicyTest, RecordTriggersReportTimer) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(MiB(500));
  ASSERT_TRUE(policy.Init());
  ASSERT_FALSE(report_timer->IsRunning());

  policy.Record(0);

  EXPECT_TRUE(report_timer->IsRunning());
  EXPECT_EQ(report_timer->GetCurrentDelay(), base::Days(7));
}

TEST_F(VmmSwapTbwPolicyTest, ReportTBWOfAWeek) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(MiB(500));
  ASSERT_TRUE(policy.Init());
  ASSERT_FALSE(report_timer->IsRunning());

  task_environment_.FastForwardBy(base::Days(1));
  policy.Record(MiB(100));
  task_environment_.FastForwardBy(base::Days(1));
  policy.Record(MiB(200));
  task_environment_.FastForwardBy(base::Days(1));
  policy.Record(MiB(300));
  // After 1 week
  task_environment_.FastForwardBy(base::Days(5));

  int min_mib = 192;
  int max_mib = 20 * 1024;
  int buckets = 50;
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsTotalBytesWrittenInAWeek, 600,
                                   min_mib, max_mib, buckets))
      .Times(1);
  report_timer->Fire();
  // Timer keep running.
  EXPECT_TRUE(report_timer->IsRunning());
  EXPECT_EQ(report_timer->GetCurrentDelay(), base::Days(7));
}

TEST_F(VmmSwapTbwPolicyTest, ReportTBWOfAWeekDelayedReport) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(MiB(500));
  ASSERT_TRUE(policy.Init());
  ASSERT_FALSE(report_timer->IsRunning());

  task_environment_.FastForwardBy(base::Days(1));
  policy.Record(MiB(100));
  task_environment_.FastForwardBy(base::Days(1));
  policy.Record(MiB(200));
  task_environment_.FastForwardBy(base::Days(1));
  policy.Record(MiB(300));
  // After more than 1 week (8 days)
  task_environment_.FastForwardBy(base::Days(6));
  // The bytes written for the next week is reported for the next week.
  policy.Record(MiB(400));

  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 600, _, _, _))
      .Times(1);
  report_timer->Fire();

  // The next week data is reported.
  task_environment_.FastForwardBy(base::Days(6));
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 400, _, _, _))
      .Times(1);
  report_timer->Fire();
}

TEST_F(VmmSwapTbwPolicyTest, ReportTBWOfAWeekDelayedReportForMultipleWeeks) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  policy.SetTargetTbwPerDay(MiB(500));
  ASSERT_TRUE(policy.Init());
  ASSERT_FALSE(report_timer->IsRunning());

  for (int i = 0; i < 100; i++) {
    policy.Record(MiB(100));
    task_environment_.FastForwardBy(base::Days(1));
  }

  // Reports up to 4 weeks data.
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 700, _, _, _))
      .Times(4);
  report_timer->Fire();

  // The last reported at is reset and report zero for the next week.
  task_environment_.FastForwardBy(base::Days(7));
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 0, _, _, _))
      .Times(1);
  report_timer->Fire();
}

TEST_F(VmmSwapTbwPolicyTest, InitTriggerReportTimerIfPreviouslyReported) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy before_policy =
      VmmSwapTbwPolicy(GetMetricsRef(), history_file_path_,
                       std::make_unique<base::MockRepeatingTimer>());
  VmmSwapTbwPolicy after_policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  before_policy.SetTargetTbwPerDay(MiB(500));
  after_policy.SetTargetTbwPerDay(MiB(500));

  ASSERT_TRUE(before_policy.Init());
  task_environment_.FastForwardBy(base::Days(1));
  before_policy.Record(MiB(100));
  task_environment_.FastForwardBy(base::Days(3));

  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, _, _, _, _))
      .Times(0);
  ASSERT_TRUE(after_policy.Init());
  EXPECT_TRUE(report_timer->IsRunning());
  EXPECT_EQ(report_timer->GetCurrentDelay(), base::Days(4));

  task_environment_.FastForwardBy(base::Days(2));
  after_policy.Record(MiB(200));
  // After 1 week of the first Record() at before_policy.
  task_environment_.FastForwardBy(base::Days(2));
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 300, _, _, _))
      .Times(1);
  report_timer->Fire();
  // The timer is updated to weekly.
  EXPECT_EQ(report_timer->GetCurrentDelay(), base::Days(7));
}

TEST_F(VmmSwapTbwPolicyTest,
       InitTriggerReportTimerIfPreviouslyReportedMoreThanAWeekAgo) {
  base::MockRepeatingTimer* report_timer = report_timer_.get();
  VmmSwapTbwPolicy before_policy =
      VmmSwapTbwPolicy(GetMetricsRef(), history_file_path_,
                       std::make_unique<base::MockRepeatingTimer>());
  VmmSwapTbwPolicy after_policy = VmmSwapTbwPolicy(
      GetMetricsRef(), history_file_path_, std::move(report_timer_));
  before_policy.SetTargetTbwPerDay(MiB(500));
  after_policy.SetTargetTbwPerDay(MiB(500));

  ASSERT_TRUE(before_policy.Init());
  task_environment_.FastForwardBy(base::Days(1));
  before_policy.Record(MiB(100));
  task_environment_.FastForwardBy(base::Days(15));

  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 100, _, _, _))
      .Times(1);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsTotalBytesWrittenInAWeek, 0, _, _, _))
      .Times(1);
  ASSERT_TRUE(after_policy.Init());
  EXPECT_TRUE(report_timer->IsRunning());
  EXPECT_EQ(report_timer->GetCurrentDelay(), base::Days(7));
}

}  // namespace vm_tools::concierge
