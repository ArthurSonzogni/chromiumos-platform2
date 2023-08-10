// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time_override.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/delegate/constants.h"
#include "diagnostics/cros_healthd/delegate/fetchers/boot_performance.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

using ::ash::cros_healthd::mojom::ErrorType;

const char kFakeBiosTimes[] =
    "texts\n"
    "503:starting to initialize TPM                        50,000 (502)\n"
    "random texts   \n"
    "504:finished TPM initialization                       60,000 (10,000)\n"
    "Total Time: 10,111,111";
const double kFirmwareSeconds = 10.111111;

// TPM initialization time is equal to
// The time of "starting to initialize TPM" - the time of "finished TPM
// initialization" in kFakeBiosTimes.
// Should be 60000 - 50000 = 10000, which is 0.01 in seconds.
const double kTpmInitializationSeconds = 0.01;

const std::vector<std::pair<std::string, std::string>> kBootstatEventContents =
    {
        {"uptime-pre-startup", "1.0 10.0"},
        {"uptime-post-startup", "2.0 20.0"},
        {"uptime-chrome-exec", "4.0 40.0"},
        {"uptime-boot-complete", "8.0 80.0\n123.0 456.0"},
};
const std::map<std::string, double> kBootstatMetrics = {
    {bootstat_event::kPreStartup, 1.0},
    {bootstat_event::kPostStartup, 2.0},
    {bootstat_event::kChromeExec, 4.0},
    {bootstat_event::kBootComplete, 8.0},
};

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
    const ash::cros_healthd::mojom::BootPerformanceResultPtr& result) {
  ASSERT_TRUE(result->is_boot_performance_info());

  const auto& info = result->get_boot_performance_info();
  EXPECT_EQ(info->shutdown_reason, "N/A");
  EXPECT_NEAR(info->shutdown_timestamp, 0.0, 0.1);
  EXPECT_NEAR(info->shutdown_seconds, 0.0, 0.1);
}

base::Time FakeTimeNow() {
  return base::Time::FromDoubleT(kCurrentTimestamp);
}

class BootPerformanceFetcherTest : public ::testing::Test {
 protected:
  BootPerformanceFetcherTest() = default;
  BootPerformanceFetcherTest(const BootPerformanceFetcherTest&) = delete;
  BootPerformanceFetcherTest& operator=(const BootPerformanceFetcherTest&) =
      delete;

  void SetUp() override {
    PopulateBiosTimesFile();
    PopulateBootStatFiles();
    PopulateProcUptimeFile();
    PopulatePowerdLog();
    PopulateShutdownMetricsDir();
  }

  void PopulateBiosTimesFile(const std::string& content = kFakeBiosTimes) {
    const auto path = GetRootedPath(path::kBiosTimes);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  base::FilePath GetBootStatPath(const std::string& event) {
    return GetRootedPath(path::kBootstatDir).Append(event);
  }

  void PopulateBootStatFiles() {
    for (const auto& [event, content] : kBootstatEventContents) {
      const auto path = GetBootStatPath(event);
      ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
    }
  }

  void PopulateProcUptimeFile(const std::string& content = kFakeProcUptime) {
    const auto path = GetRootedPath(path::kProcUptime);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  void PopulatePowerdLog(const std::string& content = kFakePowerdShutdownLog) {
    const auto path = GetRootedPath(path::kPreviousPowerdLog);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, content));
  }

  void PopulateShutdownMetricsDir() {
    const auto path = GetRootedPath(path::kShutdownMetrics);
    // It's a directory in DUT, but using file for simulation is easier.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, ""));

    base::Time time;
    ASSERT_TRUE(
        base::Time::FromUTCString(kFakeShutdownMetricsModifiedTime, &time));

    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
    ASSERT_TRUE(file.SetTimes(time, time));
    file.Close();
  }

 private:
  ScopedRootDirOverrides root_overrides_;
  base::subtle::ScopedTimeClockOverrides clock_overrides_{&FakeTimeNow, nullptr,
                                                          nullptr};
};

TEST_F(BootPerformanceFetcherTest, FetchBootPerformanceInfo) {
  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_boot_performance_info());

  const auto& info = result->get_boot_performance_info();
  EXPECT_NEAR(
      info->boot_up_seconds,
      kFirmwareSeconds + kBootstatMetrics.at(bootstat_event::kBootComplete),
      0.1);
  EXPECT_NEAR(info->boot_up_timestamp, kBootUpTimestamp, 0.1);

  base::Time time;
  ASSERT_TRUE(
      base::Time::FromUTCString(kFakeShutdownMetricsModifiedTime, &time));
  EXPECT_EQ(info->shutdown_reason, kShutdownReason);
  EXPECT_NEAR(info->shutdown_timestamp, time.ToDoubleT(), 0.1);
  EXPECT_NEAR(info->shutdown_seconds, kShutdownSeconds, 0.1);
  EXPECT_NEAR(info->tpm_initialization_seconds->value,
              kTpmInitializationSeconds, 0.1);
  EXPECT_NEAR(info->power_on_to_kernel_seconds.value(), kFirmwareSeconds, 0.1);
  EXPECT_NEAR(info->kernel_to_pre_startup_seconds.value(),
              kBootstatMetrics.at(bootstat_event::kPreStartup), 0.1);
  EXPECT_NEAR(info->kernel_to_post_startup_seconds.value(),
              kBootstatMetrics.at(bootstat_event::kPostStartup), 0.1);
  EXPECT_NEAR(info->startup_to_chrome_exec_seconds.value(),
              kBootstatMetrics.at(bootstat_event::kChromeExec) -
                  kBootstatMetrics.at(bootstat_event::kPreStartup),
              0.1);
  EXPECT_NEAR(info->chrome_exec_to_login_seconds.value(),
              kBootstatMetrics.at(bootstat_event::kBootComplete) -
                  kBootstatMetrics.at(bootstat_event::kChromeExec),
              0.1);
}

TEST_F(BootPerformanceFetcherTest, TestNoBiosTimesInfo) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kBiosTimes)));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestNoUptimeLogInfo) {
  ASSERT_TRUE(brillo::DeleteFile(GetBootStatPath("uptime-boot-complete")));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestNoProcUptimeInfo) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kProcUptime)));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongBiosTimesInfo) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kBiosTimes)));
  PopulateBiosTimesFile("Wrong content");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongBiosTimesInfo2) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kBiosTimes)));
  PopulateBiosTimesFile("Wrong content, Total Time: abcd");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongUptimeLogInfo) {
  const auto path = GetBootStatPath("uptime-boot-complete");
  ASSERT_TRUE(brillo::DeleteFile(path));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(path, "Wrong content"));

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kFileReadError);
}

TEST_F(BootPerformanceFetcherTest, TestWrongProcUptimeInfo) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kProcUptime)));
  PopulateProcUptimeFile("Wrong content");

  auto result = FetchBootPerformanceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, ErrorType::kParseError);
}

TEST_F(BootPerformanceFetcherTest, TestPowerdRebootLog) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kPreviousPowerdLog)));
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
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kPreviousPowerdLog)));

  VerifyDefaultShutdownInfo(FetchBootPerformanceInfo());
}

TEST_F(BootPerformanceFetcherTest, TestNoShutdownMetrics) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kShutdownMetrics)));

  VerifyDefaultShutdownInfo(FetchBootPerformanceInfo());
}

TEST_F(BootPerformanceFetcherTest, TestWrongPowerdLog) {
  ASSERT_TRUE(brillo::DeleteFile(GetRootedPath(path::kPreviousPowerdLog)));
  PopulatePowerdLog("Wrong content");

  VerifyDefaultShutdownInfo(FetchBootPerformanceInfo());
}

}  // namespace
}  // namespace diagnostics
