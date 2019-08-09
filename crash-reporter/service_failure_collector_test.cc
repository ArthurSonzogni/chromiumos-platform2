// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/service_failure_collector.h"

#include <unistd.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using base::FilePath;

namespace {

bool s_metrics = false;

// Source tree log config file name.
const char kLogConfigFileName[] = "crash_reporter_logs.conf";

const char kTestFilename[] = "test-service-failure";
const char kTestCrashDirectory[] = "test-crash-directory";

bool IsMetrics() {
  return s_metrics;
}

}  // namespace

class ServiceFailureCollectorMock : public ServiceFailureCollector {
 public:
  MOCK_METHOD0(SetUpDBus, void());
};

class ServiceFailureCollectorTest : public ::testing::Test {
  void SetUp() {
    s_metrics = true;

    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(IsMetrics, false);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_path_ = scoped_temp_dir_.GetPath().Append(kTestFilename);
    collector_.failure_report_path_ = test_path_.value();

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    CreateDirectory(test_crash_directory_);
    collector_.set_crash_directory_for_test(test_crash_directory_);
    collector_.set_log_config_path(
        test_util::GetTestDataPath(kLogConfigFileName).value());
  }

 protected:
  ServiceFailureCollectorMock collector_;
  base::ScopedTempDir scoped_temp_dir_;
  FilePath test_path_;
  FilePath test_crash_directory_;
};

TEST_F(ServiceFailureCollectorTest, CollectOKMain) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash main process (2563) terminated with status 2\n"));
  collector_.SetServiceName("crash-crash");
  EXPECT_TRUE(collector_.Collect());
  EXPECT_FALSE(IsDirectoryEmpty(test_crash_directory_));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "service_failure_crash_crash.*.meta", NULL));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "service_failure_crash_crash.*.log", NULL));
}

TEST_F(ServiceFailureCollectorTest, CollectOKPreStart) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash pre-start process (2563) terminated with status 2\n"));
  collector_.SetServiceName("crash-crash");
  EXPECT_TRUE(collector_.Collect());
  EXPECT_FALSE(IsDirectoryEmpty(test_crash_directory_));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "service_failure_crash_crash.*.meta", NULL));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "service_failure_crash_crash.*.log", NULL));
}

TEST_F(ServiceFailureCollectorTest, FailureReportDoesNotExist) {
  // Service failure report file doesn't exist.
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}

TEST_F(ServiceFailureCollectorTest, EmptyFailureReport) {
  // Service failure report file exists, but doesn't have the expected contents.
  ASSERT_TRUE(test_util::CreateFile(test_path_, ""));
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}

TEST_F(ServiceFailureCollectorTest, FeedbackNotAllowed) {
  // Feedback not allowed.
  s_metrics = false;
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash main process (2563) terminated with status 2\n"));
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}
