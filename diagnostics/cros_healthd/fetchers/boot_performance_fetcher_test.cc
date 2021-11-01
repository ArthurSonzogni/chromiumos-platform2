// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/boot_performance_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

using ::chromeos::cros_healthd::mojom::ErrorType;
using ::testing::Return;

const char kFakeBiosTimes[] = "texts\n...\n\nTotal Time: 10,111,111";
const char kFakeUptimeLog[] = "7.666666666\n17.000000000";
const char kFakeProcUptime[] = "100.33 126.43";
const char kFakePowerdShutdownLog[] =
    "texts\n"
    "2020-05-03T12:12:28.500000Z INFO powerd: [daemon.cc(1435)] "
    "Shutting down, reason: other-request-to-powerd\ntexts\ntexts\n";
const char kFakePowerdRebootLog[] =
    "texts\n"
    "2020-05-03T12:12:28.500000Z INFO powerd: [daemon.cc(1435)] "
    "Restarting, reason: other-request-to-powerd\ntexts\ntexts\n";
const char kFakeShutdownMetricsModifiedTime[] = "2020-05-03T12:12:30.000000";
const double kCurrentTimestamp = 1000.0;

// Answers
// Boot up seconds is equal to
// "Total Time" in bios time + the first record from the up time log.
// 10.111111 + 7.666666666 = 17.777777666.
const double kBootUpSeconds = 17.777777;

// Boot up timestamp is equal to
// Current time - the first record of proc up time - bios time.
// 1000 - 100.33 - 10.111111 = 889.558889.
const double kBootUpTimestamp = 889.558889;

// Shutdown reason
// This can be found in powerd shutdown log.
const char kShutdownReason[] = "other-request-to-powerd";

// Shutdown seconds is equal to
// The modified time of metrics directory - the time we parse in powerd log.
// Should be 2020-05-03T12:12:30.000000 - 2020-05-03T12:12:28.500000 = 1.5
const double kShutdownSeconds = 1.5;

void VerifyDefaultShutdownInfo(
    const chromeos::cros_healthd::mojom::BootPerformanceResultPtr& result) {
  ASSERT_TRUE(result->is_boot_performance_info());

  const auto& info = result->get_boot_performance_info();
  EXPECT_EQ(info->shutdown_reason, "N/A");
  EXPECT_NEAR(info->shutdown_timestamp, 0.0, 0.1);
  EXPECT_NEAR(info->shutdown_seconds, 0.0, 0.1);
}

class BootPerformanceFetcherTest : public ::testing::Test {
 protected:
  BootPerformanceFetcherTest() = default;
  BootPerformanceFetcherTest(const BootPerformanceFetcherTest&) = delete;
  BootPerformanceFetcherTest& operator=(const BootPerformanceFetcherTest&) =
      delete;

  void SetUp() override {
    PopulateBiosTimesFile();
    PopulateUptimeLogFile();
    PopulateProcUptimeFile();
    PopulatePowerdLog();
    PopulateShutdownMetricsDir();
  }

  void PopulateBiosTimesFile(const std::string& content = kFakeBiosTimes) {
    const auto path = root_dir().Append(kRelativeBiosTimesPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  void PopulateUptimeLogFile(const std::string& content = kFakeUptimeLog) {
    const auto path = root_dir().Append(kRelativeUptimeLoginPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  void PopulateProcUptimeFile(const std::string& content = kFakeProcUptime) {
    const auto path = GetProcUptimePath(root_dir());
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  void PopulatePowerdLog(const std::string& content = kFakePowerdShutdownLog) {
    const auto path = root_dir().Append(kRelativePreviousPowerdLogPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  void PopulateShutdownMetricsDir() {
    const auto path = root_dir().Append(kRelativeShutdownMetricsPath);
    // It's a directory in DUT, but using file for simulation is easier.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, ""));

    base::Time time;
    ASSERT_TRUE(
        base::Time::FromUTCString(kFakeShutdownMetricsModifiedTime, &time));

    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
    ASSERT_TRUE(file.SetTimes(time, time));
    file.Close();
  }

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

  const MockContext& mock_context() const { return mock_context_; }

  chromeos::cros_healthd::mojom::BootPerformanceResultPtr
  FetchBootPerformanceInfo() {
    return boot_performance_fetcher_.FetchBootPerformanceInfo();
  }

 private:
  MockContext mock_context_;
  BootPerformanceFetcher boot_performance_fetcher_{&mock_context_};
};

TEST_F(BootPerformanceFetcherTest, FetchBootPerformanceInfo) {
  EXPECT_CALL(mock_context(), time())
      .WillOnce(Return(base::Time::FromDoubleT(kCurrentTimestamp)));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_boot_performance_info());

  const auto& info = result->get_boot_performance_info();
  EXPECT_NEAR(info->boot_up_seconds, kBootUpSeconds, 0.1);
  EXPECT_NEAR(info->boot_up_timestamp, kBootUpTimestamp, 0.1);

  base::Time time;
  ASSERT_TRUE(
      base::Time::FromUTCString(kFakeShutdownMetricsModifiedTime, &time));
  EXPECT_EQ(info->shutdown_reason, kShutdownReason);
  EXPECT_NEAR(info->shutdown_timestamp, time.ToDoubleT(), 0.1);
  EXPECT_NEAR(info->shutdown_seconds, kShutdownSeconds, 0.1);
}

TEST_F(BootPerformanceFetcherTest, TestNoBiosTimesInfo) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeBiosTimesPath)));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestNoUptimeLogInfo) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeUptimeLoginPath)));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestNoProcUptimeInfo) {
  ASSERT_TRUE(base::DeleteFile(GetProcUptimePath(root_dir())));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongBiosTimesInfo) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeBiosTimesPath)));
  PopulateBiosTimesFile("Wrong content");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongBiosTimesInfo2) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeBiosTimesPath)));
  PopulateBiosTimesFile("Wrong content, Total Time: abcd");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongUptimeLogInfo) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeUptimeLoginPath)));
  PopulateUptimeLogFile("Wrong content");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongProcUptimeInfo) {
  ASSERT_TRUE(base::DeleteFile(GetProcUptimePath(root_dir())));
  PopulateProcUptimeFile("Wrong content");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestPowerdRebootLog) {
  ASSERT_TRUE(
      base::DeleteFile(root_dir().Append(kRelativePreviousPowerdLogPath)));
  PopulatePowerdLog(kFakePowerdRebootLog);

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_boot_performance_info());

  const auto& info = result->get_boot_performance_info();
  base::Time time;
  ASSERT_TRUE(
      base::Time::FromUTCString(kFakeShutdownMetricsModifiedTime, &time));
  EXPECT_EQ(info->shutdown_reason, kShutdownReason);
  EXPECT_NEAR(info->shutdown_timestamp, time.ToDoubleT(), 0.1);
  EXPECT_NEAR(info->shutdown_seconds, kShutdownSeconds, 0.1);
}

TEST_F(BootPerformanceFetcherTest, TestNoPowerdLog) {
  ASSERT_TRUE(
      base::DeleteFile(root_dir().Append(kRelativePreviousPowerdLogPath)));

  VerifyDefaultShutdownInfo(FetchBootPerformanceInfo());
}

TEST_F(BootPerformanceFetcherTest, TestNoShutdownMetrics) {
  ASSERT_TRUE(
      base::DeleteFile(root_dir().Append(kRelativeShutdownMetricsPath)));

  VerifyDefaultShutdownInfo(FetchBootPerformanceInfo());
}

TEST_F(BootPerformanceFetcherTest, TestWrongPowerdLog) {
  ASSERT_TRUE(
      base::DeleteFile(root_dir().Append(kRelativePreviousPowerdLogPath)));
  PopulatePowerdLog("Wrong content");

  VerifyDefaultShutdownInfo(FetchBootPerformanceInfo());
}

}  // namespace
}  // namespace diagnostics
