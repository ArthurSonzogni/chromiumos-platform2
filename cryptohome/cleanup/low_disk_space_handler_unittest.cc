// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cleanup/low_disk_space_handler.h"

#include <utility>

#include <base/test/bind.h>
#include <base/test/test_mock_time_task_runner.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/cleanup/disk_cleanup.h"
#include "cryptohome/cleanup/mock_disk_cleanup.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_cache.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/mock_homedirs.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

namespace cryptohome {

class LowDiskSpaceHandlerTest : public ::testing::Test {
 public:
  LowDiskSpaceHandlerTest()
      : handler_(
            &homedirs_, &keyset_management_, &platform_, &timestamp_cache_) {}
  virtual ~LowDiskSpaceHandlerTest() = default;

  void SetUp() {
    handler_.set_disk_cleanup(&disk_cleanup_);
    handler_.SetLowDiskSpaceCallback(base::BindRepeating([](uint64_t) {}));
    handler_.SetUpdateUserActivityTimestampCallback(
        base::BindRepeating([]() {}));

    EXPECT_CALL(platform_, GetCurrentTime()).WillRepeatedly(Invoke([&]() {
      return current_time_;
    }));

    EXPECT_CALL(disk_cleanup_, AmountOfFreeDiskSpace())
        .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup + 1));
    EXPECT_CALL(disk_cleanup_, GetFreeDiskSpaceState(_))
        .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kAboveThreshold));
    EXPECT_CALL(disk_cleanup_, IsFreeableDiskSpaceAvailable())
        .WillRepeatedly(Return(false));

    EXPECT_TRUE(handler_.Init(base::BindRepeating(
        &LowDiskSpaceHandlerTest::PostDelayedTask, base::Unretained(this))));
  }

  void TearDown() {
    task_runner_->RunUntilIdle();
    handler_.Stop();
    task_runner_->RunUntilIdle();
  }

 protected:
  bool PostDelayedTask(const base::Location& from_here,
                       base::OnceClosure task,
                       const base::TimeDelta& delay) {
    return task_runner_->PostDelayedTask(from_here, std::move(task), delay);
  }

  StrictMock<MockPlatform> platform_;
  StrictMock<MockHomeDirs> homedirs_;
  StrictMock<MockKeysetManagement> keyset_management_;
  StrictMock<UserOldestActivityTimestampCache> timestamp_cache_;
  StrictMock<MockDiskCleanup> disk_cleanup_;
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner_ =
      new base::TestMockTimeTaskRunner();

  LowDiskSpaceHandler handler_;

  base::Time current_time_;
};

TEST_F(LowDiskSpaceHandlerTest, RunCleanupAtStartup) {
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
}

TEST_F(LowDiskSpaceHandlerTest, DontRunIfStopped) {
  handler_.Stop();
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).Times(0);

  task_runner_->RunUntilIdle();
}

TEST_F(LowDiskSpaceHandlerTest, RunPeriodicCheck) {
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
  task_runner_->RunUntilIdle();

  // Must check for low disk space.
  EXPECT_CALL(disk_cleanup_, AmountOfFreeDiskSpace())
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1));

  task_runner_->FastForwardBy(handler_.low_disk_notification_period());
  task_runner_->RunUntilIdle();
}

TEST_F(LowDiskSpaceHandlerTest, CallPeriodicCallback) {
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
  task_runner_->RunUntilIdle();

  EXPECT_CALL(disk_cleanup_, GetFreeDiskSpaceState(_))
      .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kNeedNormalCleanup));

  // First call will perform cleanup.
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));

  for (int i = 0; i < 10; i++) {
    uint64_t free_space = kFreeSpaceThresholdToTriggerCleanup - 115 - i;
    bool callback_called = false;

    EXPECT_CALL(disk_cleanup_, AmountOfFreeDiskSpace())
        .WillOnce(Return(free_space));

    handler_.SetLowDiskSpaceCallback(
        base::BindLambdaForTesting([&](uint64_t val) {
          EXPECT_EQ(free_space, val);
          EXPECT_FALSE(callback_called);
          callback_called = true;
        }));

    task_runner_->FastForwardBy(handler_.low_disk_notification_period());
    task_runner_->RunUntilIdle();

    EXPECT_TRUE(callback_called);
  }
}

TEST_F(LowDiskSpaceHandlerTest, RunPeriodicCleanup) {
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
  task_runner_->RunUntilIdle();

  // Allow cleanup to be triggered.
  EXPECT_CALL(disk_cleanup_, AmountOfFreeDiskSpace())
      .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup - 1));
  EXPECT_CALL(disk_cleanup_, GetFreeDiskSpaceState(_))
      .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kNeedNormalCleanup));

  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
  task_runner_->FastForwardBy(handler_.low_disk_notification_period());
  task_runner_->RunUntilIdle();

  auto delta = base::TimeDelta::FromMilliseconds(kAutoCleanupPeriodMS + 1);

  for (int i = 0; i < 50; i++) {
    EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
    current_time_ += delta;
    task_runner_->FastForwardBy(delta);
    task_runner_->RunUntilIdle();
  }
}

TEST_F(LowDiskSpaceHandlerTest, RunPeriodicCleanupEnrolled) {
  EXPECT_CALL(disk_cleanup_, IsFreeableDiskSpaceAvailable())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
  task_runner_->RunUntilIdle();

  // Allow cleanup to be triggered.
  EXPECT_CALL(disk_cleanup_, AmountOfFreeDiskSpace())
      .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup - 1));
  EXPECT_CALL(disk_cleanup_, GetFreeDiskSpaceState(_))
      .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kNeedNormalCleanup));

  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
  task_runner_->FastForwardBy(handler_.low_disk_notification_period());
  task_runner_->RunUntilIdle();

  auto delta = handler_.low_disk_notification_period() +
               base::TimeDelta::FromMilliseconds(1);

  for (int i = 0; i < 50; i++) {
    EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillOnce(Return(true));
    current_time_ += delta;
    task_runner_->FastForwardBy(delta);
    task_runner_->RunUntilIdle();
  }
}

TEST_F(LowDiskSpaceHandlerTest, RunPeriodicLastActivityUpdate) {
  EXPECT_CALL(disk_cleanup_, FreeDiskSpace()).WillRepeatedly(Return(true));

  for (int i = 0; i < 50; i++) {
    bool callback_called = false;

    handler_.SetUpdateUserActivityTimestampCallback(
        base::BindLambdaForTesting([&]() {
          EXPECT_FALSE(callback_called);
          callback_called = true;
        }));

    auto delta = handler_.update_user_activity_timestamp_period() +
                 base::TimeDelta::FromMilliseconds(1);

    current_time_ += delta;
    task_runner_->FastForwardBy(delta);
    task_runner_->RunUntilIdle();

    EXPECT_TRUE(callback_called);
  }
}

}  // namespace cryptohome
