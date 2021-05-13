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
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

using ::chromeos::cros_healthd::mojom::ErrorType;
using ::testing::Return;

namespace {

const char kFakeBiosTimes[] = "texts\n...\n\nTotal Time: 10,111,111";
const char kFakeUptimeLog[] = "7.666666666\n17.000000000";
const char kFakeProcUptime[] = "100.33 126.43";
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

}  // namespace

class BootPerformanceFetcherTest : public ::testing::Test {
 protected:
  BootPerformanceFetcherTest() = default;
  BootPerformanceFetcherTest(const BootPerformanceFetcherTest&) = delete;
  BootPerformanceFetcherTest& operator=(const BootPerformanceFetcherTest&) =
      delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());

    PopulateBiosTimesFile();
    PopulateUptimeLogFile();
    PopulateProcUptimeFile();
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

}  // namespace diagnostics
