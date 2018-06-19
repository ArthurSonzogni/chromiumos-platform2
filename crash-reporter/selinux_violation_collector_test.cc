// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/selinux_violation_collector.h"

#include <unistd.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <base/files/file_enumerator.h>

using base::FilePath;

namespace {

bool s_metrics = false;

// Source tree log config file name.
constexpr char kLogConfigFileName[] = "crash_reporter_logs.conf";

constexpr char kTestFilename[] = "test-selinux-violation";
constexpr char kTestCrashDirectory[] = "test-crash-directory";

constexpr char TestSELinuxViolationMessage[] =
    "sssss-selinux-init\n"
    "comm\001init\002scontext\001context1\002\n"
    "SELINUX VIOLATION TRIGGERED FOR init AT context1.\n";

constexpr char TestSELinuxViolationMessageContent[] =
    "SELINUX VIOLATION TRIGGERED FOR init AT context1.\n";

bool IsMetrics() {
  return s_metrics;
}

void CountCrash() {}

// Returns true if at least one file in this directory matches the pattern.
// found_file_path is not assigned if found_file_path is nullptr.
// Only the first found path is stored into found_file_path.
bool DirectoryHasFileWithPattern(const FilePath& directory,
                                 const std::string& pattern,
                                 FilePath* found_file_path) {
  base::FileEnumerator enumerator(
      directory, false, base::FileEnumerator::FileType::FILES, pattern);
  FilePath path = enumerator.Next();
  if (!path.empty() && found_file_path)
    *found_file_path = path;
  return !path.empty();
}

}  // namespace

class SELinuxViolationCollectorMock : public SELinuxViolationCollector {
 public:
  MOCK_METHOD0(SetUpDBus, void());
};

class SELinuxViolationCollectorTest : public ::testing::Test {
  void SetUp() {
    s_metrics = true;

    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(CountCrash, IsMetrics);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_path_ = scoped_temp_dir_.GetPath().Append(kTestFilename);
    collector_.set_violation_report_path_for_testing(test_path_);

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    CreateDirectory(test_crash_directory_);
    collector_.set_crash_directory_for_test(test_crash_directory_);
    collector_.set_log_config_path(kLogConfigFileName);
  }

 protected:
  void WriteStringToFile(const FilePath& file_path, const char* data) {
    ASSERT_EQ(strlen(data), base::WriteFile(file_path, data, strlen(data)));
  }

  SELinuxViolationCollectorMock collector_;
  base::ScopedTempDir scoped_temp_dir_;
  FilePath test_path_;
  FilePath test_crash_directory_;
};

TEST_F(SELinuxViolationCollectorTest, CollectOK) {
  // Collector produces a violation report.
  collector_.set_developer_image_for_testing();
  WriteStringToFile(test_path_, TestSELinuxViolationMessage);
  EXPECT_TRUE(collector_.Collect());
  EXPECT_FALSE(IsDirectoryEmpty(test_crash_directory_));
  EXPECT_TRUE(DirectoryHasFileWithPattern(test_crash_directory_,
                                          "selinux_violation.*.meta", NULL));
  FilePath file_path;
  EXPECT_TRUE(DirectoryHasFileWithPattern(
      test_crash_directory_, "selinux_violation.*.log", &file_path));
  std::string content;
  base::ReadFileToString(file_path, &content);
  EXPECT_STREQ(content.c_str(), TestSELinuxViolationMessageContent);
}

TEST_F(SELinuxViolationCollectorTest, CollectSample) {
  // Collector produces a violation report.
  collector_.set_fake_random_for_statistic_sampling(1);
  WriteStringToFile(test_path_, TestSELinuxViolationMessage);
  EXPECT_TRUE(collector_.Collect());
  EXPECT_FALSE(IsDirectoryEmpty(test_crash_directory_));
  EXPECT_TRUE(DirectoryHasFileWithPattern(test_crash_directory_,
                                          "selinux_violation.*.meta", NULL));
  FilePath file_path;
  EXPECT_TRUE(DirectoryHasFileWithPattern(
      test_crash_directory_, "selinux_violation.*.log", &file_path));
  std::string content;
  base::ReadFileToString(file_path, &content);
  EXPECT_STREQ(content.c_str(), TestSELinuxViolationMessageContent);
}

TEST_F(SELinuxViolationCollectorTest, FailureReportDoesNotExist) {
  // SELinux violation report file doesn't exist.
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}

TEST_F(SELinuxViolationCollectorTest, EmptyFailureReport) {
  // SELinux violation report file exists, but doesn't have the expected
  // contents.
  WriteStringToFile(test_path_, "");
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}

TEST_F(SELinuxViolationCollectorTest, FeedbackNotAllowed) {
  // Feedback not allowed.
  s_metrics = false;
  WriteStringToFile(test_path_, TestSELinuxViolationMessage);
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}

TEST_F(SELinuxViolationCollectorTest, DroppedSample) {
  // Drop sample.
  collector_.set_fake_random_for_statistic_sampling(999);
  WriteStringToFile(test_path_, TestSELinuxViolationMessage);
  EXPECT_TRUE(collector_.Collect());
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}
