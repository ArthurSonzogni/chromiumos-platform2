// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include "base/file_util.h"
#include "base/string_util.h"
#include "chromeos/syslog_logging.h"
#include "chromeos/test_helpers.h"
#include "crash-reporter/unclean_shutdown_collector.h"
#include "gflags/gflags.h"
#include "gtest/gtest.h"

static int s_crashes = 0;
static bool s_metrics = true;

static const char kTestLowBattery[] = "test/low_battery";
static const char kTestSuspended[] = "test/suspended";
static const char kTestUnclean[] = "test/unclean";

using ::chromeos::FindLog;

void CountCrash() {
  ++s_crashes;
}

bool IsMetrics() {
  return s_metrics;
}

class UncleanShutdownCollectorTest : public ::testing::Test {
  void SetUp() {
    s_crashes = 0;
    collector_.Initialize(CountCrash,
                          IsMetrics);
    rmdir("test");
    test_unclean_ = FilePath(kTestUnclean);
    collector_.unclean_shutdown_file_ = kTestUnclean;
    file_util::Delete(test_unclean_, true);
    // Set up alternate power manager tracing files as well
    collector_.powerd_suspended_file_ = FilePath(kTestSuspended);
    collector_.powerd_low_battery_file_ = FilePath(kTestLowBattery);
    chromeos::ClearLog();
  }
 protected:
  void WriteStringToFile(const FilePath &file_path,
                         const char *data) {
    ASSERT_EQ(strlen(data),
              file_util::WriteFile(file_path, data, strlen(data)));
  }

  UncleanShutdownCollector collector_;
  FilePath test_unclean_;
};

TEST_F(UncleanShutdownCollectorTest, EnableWithoutParent) {
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(file_util::PathExists(test_unclean_));
}

TEST_F(UncleanShutdownCollectorTest, EnableWithParent) {
  mkdir("test", 0777);
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(file_util::PathExists(test_unclean_));
}

TEST_F(UncleanShutdownCollectorTest, EnableCannotWrite) {
  collector_.unclean_shutdown_file_ = "/bad/path";
  ASSERT_FALSE(collector_.Enable());
  ASSERT_TRUE(FindLog("Unable to create shutdown check file"));
}

TEST_F(UncleanShutdownCollectorTest, CollectTrue) {
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(file_util::PathExists(test_unclean_));
  ASSERT_TRUE(collector_.Collect());
  ASSERT_FALSE(file_util::PathExists(test_unclean_));
  ASSERT_EQ(1, s_crashes);
  ASSERT_TRUE(FindLog("Last shutdown was not clean"));
}

TEST_F(UncleanShutdownCollectorTest, CollectFalse) {
  ASSERT_FALSE(collector_.Collect());
  ASSERT_EQ(0, s_crashes);
}

TEST_F(UncleanShutdownCollectorTest, CollectDeadBatteryRunningLow) {
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(file_util::PathExists(test_unclean_));
  file_util::WriteFile(collector_.powerd_low_battery_file_, "", 0);
  ASSERT_FALSE(collector_.Collect());
  ASSERT_FALSE(file_util::PathExists(test_unclean_));
  ASSERT_FALSE(file_util::PathExists(collector_.powerd_low_battery_file_));
  ASSERT_EQ(0, s_crashes);
  ASSERT_TRUE(FindLog(
      "Unclean shutdown occurred while running with battery critically low."));
}

TEST_F(UncleanShutdownCollectorTest, CollectDeadBatterySuspended) {
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(file_util::PathExists(test_unclean_));
  file_util::WriteFile(collector_.powerd_suspended_file_, "", 0);
  ASSERT_FALSE(collector_.Collect());
  ASSERT_FALSE(file_util::PathExists(test_unclean_));
  ASSERT_FALSE(file_util::PathExists(collector_.powerd_suspended_file_));
  ASSERT_EQ(0, s_crashes);
  ASSERT_TRUE(FindLog("Unclean shutdown occurred while suspended."));
}

TEST_F(UncleanShutdownCollectorTest, Disable) {
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(file_util::PathExists(test_unclean_));
  ASSERT_TRUE(collector_.Disable());
  ASSERT_FALSE(file_util::PathExists(test_unclean_));
  ASSERT_FALSE(collector_.Collect());
}

TEST_F(UncleanShutdownCollectorTest, DisableWhenNotEnabled) {
  ASSERT_TRUE(collector_.Disable());
}

TEST_F(UncleanShutdownCollectorTest, CantDisable) {
  mkdir(kTestUnclean, 0700);
  file_util::WriteFile(test_unclean_.Append("foo"), "", 0);
  ASSERT_FALSE(collector_.Disable());
  rmdir(kTestUnclean);
}

int main(int argc, char **argv) {
  SetUpTests(&argc, argv, false);
  return RUN_ALL_TESTS();
}
