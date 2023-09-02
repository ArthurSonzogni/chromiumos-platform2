// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/generic_failure_collector.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/test_util.h"

using base::FilePath;

namespace {

using testing::Return;

// Source tree log config file name.
const char kLogConfigFileName[] = "crash_reporter_logs.conf";

const char kTestFilename[] = "test-generic-failure";
const char kTestFailureDirectory[] = "test-failure_directory";

}  // namespace

class GenericFailureCollectorMock : public GenericFailureCollector {
 public:
  GenericFailureCollectorMock()
      : GenericFailureCollector(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}

  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class GenericFailureCollectorTest : public ::testing::Test {
 public:
  GenericFailureCollectorTest() {}

  void SetUp() {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(false);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_path_ = scoped_temp_dir_.GetPath().Append(kTestFilename);
    collector_.failure_report_path_ = test_path_.value();

    test_failure_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestFailureDirectory);
    CreateDirectory(test_failure_directory_);
    collector_.set_crash_directory_for_test(test_failure_directory_);
    collector_.set_log_config_path(
        test_util::GetTestDataPath(kLogConfigFileName,
                                   /*use_testdata=*/false)
            .value());
  }

 protected:
  GenericFailureCollectorMock collector_;
  base::ScopedTempDir scoped_temp_dir_;
  FilePath test_path_;
  FilePath test_failure_directory_;
};

TEST_F(GenericFailureCollectorTest, CollectOKMain) {
  // Collector produces a crash report.
  const char kLogContents[] = "generic failure for testing purposes\n";
  ASSERT_TRUE(test_util::CreateFile(test_path_, kLogContents));
  EXPECT_TRUE(collector_.Collect("generic-failure"));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_failure_directory_, "generic_failure.*.meta",
      std::string("sig=") + kLogContents));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "generic_failure.*.log", nullptr));
}

TEST_F(GenericFailureCollectorTest, SuspendExecName) {
  // Check that the suspend-failure exec name is used
  const char kLogContents[] = "suspend failure for testing purposes\n";
  ASSERT_TRUE(test_util::CreateFile(test_path_, kLogContents));
  EXPECT_TRUE(collector_.Collect(GenericFailureCollector::kSuspendFailure));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_failure_directory_, "suspend_failure.*.meta",
      std::string("sig=") + kLogContents));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "suspend_failure.*.log", nullptr));
}

TEST_F(GenericFailureCollectorTest, FailureReportDoesNotExist) {
  // Generic failure report file doesn't exist.
  EXPECT_TRUE(collector_.Collect("generic-failure"));
  EXPECT_TRUE(IsDirectoryEmpty(test_failure_directory_));
}

TEST_F(GenericFailureCollectorTest, EmptyFailureReport) {
  // Generic failure report file exists, but doesn't have the expected contents.
  ASSERT_TRUE(test_util::CreateFile(test_path_, ""));
  EXPECT_TRUE(collector_.Collect("generic-failure"));
  EXPECT_TRUE(IsDirectoryEmpty(test_failure_directory_));
}

TEST_F(GenericFailureCollectorTest, CollectOKMainServiceFailure) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash main process (2563) terminated with status 2\n"));
  EXPECT_TRUE(collector_.CollectFull(
      "service-failure-crash-crash", GenericFailureCollector::kServiceFailure,
      /*weight=*/50, /*use_log_conf_file=*/true));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));

  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.meta",
      &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.log", nullptr));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(meta_path, &contents));
  LOG(INFO) << contents;
  EXPECT_TRUE(contents.find("upload_var_weight=50") != std::string::npos)
      << contents;
}

TEST_F(GenericFailureCollectorTest, CollectOKPreStart) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash pre-start process (2563) terminated with status 2\n"));
  EXPECT_TRUE(collector_.CollectFull(
      "service-failure-crash-crash", GenericFailureCollector::kServiceFailure,
      /*weight=*/50, /*use_log_conf_file=*/true));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));

  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.meta",
      &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.log", nullptr));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(meta_path, &contents));
  EXPECT_TRUE(contents.find("upload_var_weight=50") != std::string::npos)
      << contents;
}

TEST_F(GenericFailureCollectorTest, CollectOK_UploadWeightedUMA) {
  auto metrics_lib = std::make_unique<MetricsLibraryMock>();
  MetricsLibraryMock* mock_ref = metrics_lib.get();
  collector_.set_metrics_library_for_test(std::move(metrics_lib));
  EXPECT_CALL(*mock_ref,
              SendRepeatedEnumToUMA(
                  "ChromeOS.Stability.Warning",
                  static_cast<int>(CrashCollector::Product::kPlatform),
                  static_cast<int>(CrashCollector::Product::kMaxValue) + 1, 50))
      .WillOnce(Return(true));

  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash pre-start process (2563) terminated with status 2\n"));
  EXPECT_TRUE(collector_.CollectFull(
      "service-failure-crash-crash", GenericFailureCollector::kServiceFailure,
      /*weight=*/50, /*use_log_conf_file=*/true));
}

TEST_F(GenericFailureCollectorTest, CollectFullGuestOOM) {
  std::string sig = "guest-oom-event-0xdeadbeef";
  std::string log = "killed process 1234 (0xdeadbeef)";
  // log from stdin
  ASSERT_TRUE(test_util::CreateFile(test_path_, sig + '\n' + log + '\n'));
  EXPECT_TRUE(collector_.CollectFull("guest-oom-event", "", 0,
                                     /*use_log_conf_file=*/false));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));

  base::FilePath meta_path;
  std::string contents;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "guest_oom_event.*.meta", &meta_path));
  ASSERT_TRUE(base::ReadFileToString(meta_path, &contents));
  EXPECT_TRUE(contents.find("sig=guest-oom-event-0xdeadbeef") !=
              std::string::npos)
      << contents;

  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "guest_oom_event.*.log", &meta_path));
  ASSERT_TRUE(base::ReadFileToString(meta_path, &contents));
  EXPECT_TRUE(contents.find(log) != std::string::npos) << contents;
}

struct ComputeCrashSeverityTestParams {
  std::string exec_name;
  CrashCollector::CrashSeverity expected_severity;
};

class GenericFailureCollectorCrashSeverityTest
    : public GenericFailureCollectorTest,
      public ::testing::WithParamInterface<
          test_util::ComputeCrashSeverityTestParams> {};

TEST_P(GenericFailureCollectorCrashSeverityTest, ComputeCrashSeverity) {
  const test_util::ComputeCrashSeverityTestParams& test_case = GetParam();
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity(test_case.exec_name);

  EXPECT_EQ(computed_severity.crash_severity, test_case.expected_severity);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

INSTANTIATE_TEST_SUITE_P(
    GenericFailureCollectorCrashSeverityTestSuite,
    GenericFailureCollectorCrashSeverityTest,
    testing::ValuesIn<test_util::ComputeCrashSeverityTestParams>({
        {"suspend-failure", CrashCollector::CrashSeverity::kWarning},
        {"suspend-failure-executable",
         CrashCollector::CrashSeverity::kUnspecified},
        {"service-failure", CrashCollector::CrashSeverity::kWarning},
        {"service-failure-executable", CrashCollector::CrashSeverity::kWarning},
        {"another executable", CrashCollector::CrashSeverity::kUnspecified},
    }));
