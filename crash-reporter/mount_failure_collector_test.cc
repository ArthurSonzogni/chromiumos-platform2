// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/mount_failure_collector.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/test_util.h"

namespace {

using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Return;

// Dummy log config file name.
const char kLogConfigFileName[] = "log_config_file";

// A bunch of random rules to put into the dummy log config file.
const char kLogConfigFileContents[] =
    "dumpe2fs_stateful=echo stateful\n"
    "dumpe2fs_encstateful=echo encstateful\n"
    "kernel-warning=echo dmesg\n"
    "console-ramoops=echo ramoops\n"
    "mount-encrypted=echo mount-encrypted\n"
    "shutdown_umount_failure_state=echo umount_failure_state\n"
    "umount-encrypted=echo umount-encrypted-logs\n"
    "cryptohome=echo cryptohome";

class MountFailureCollectorMock : public MountFailureCollector {
 public:
  MountFailureCollectorMock(StorageDeviceType device_type,
                            bool testonly_send_all)
      : MountFailureCollector(
            device_type,
            testonly_send_all,
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

void Initialize(MountFailureCollectorMock* collector,
                base::ScopedTempDir* scoped_tmp_dir) {
  ASSERT_TRUE(scoped_tmp_dir->CreateUniqueTempDir());
  EXPECT_CALL(*collector, SetUpDBus()).WillRepeatedly(Return());
  base::FilePath log_config_path =
      scoped_tmp_dir->GetPath().Append(kLogConfigFileName);
  ASSERT_TRUE(test_util::CreateFile(log_config_path, kLogConfigFileContents));

  collector->Initialize(false);

  collector->set_crash_directory_for_test(scoped_tmp_dir->GetPath());
  collector->set_log_config_path(log_config_path.value());
}

TEST(MountFailureCollectorTest, TestStatefulMountFailure) {
  MountFailureCollectorMock collector(StorageDeviceType::kStateful,
                                      /*testonly_send_all=*/false);
  base::ScopedTempDir tmp_dir;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir);

  EXPECT_TRUE(collector.Collect(true /* is_mount_failure */));

  // Check report collection.
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      tmp_dir.GetPath(), "mount_failure_stateful.*.meta",
      "sig=mount_failure_stateful"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "mount_failure_stateful.*.log", &report_path));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("stateful\ndmesg\nramoops\n", report_contents);
}

TEST(MountFailureCollectorTest, TestEncryptedStatefulMountFailure) {
  MountFailureCollectorMock collector(StorageDeviceType::kEncryptedStateful,
                                      /*testonly_send_all=*/false);
  base::ScopedTempDir tmp_dir;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir);

  EXPECT_TRUE(collector.Collect(true /* is_mount_failure */));

  // Check report collection.
  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "mount_failure_encstateful.*.meta", &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "mount_failure_encstateful.*.log", &report_path));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("encstateful\ndmesg\nramoops\nmount-encrypted\n", report_contents);
  // Check meta contents do *not* include weight
  std::string meta_contents;
  ASSERT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
  EXPECT_THAT(meta_contents, Not(HasSubstr("upload_var_weight=")));
}

TEST(MountFailureCollectorTest, TestUmountFailure) {
  MountFailureCollectorMock collector(StorageDeviceType::kStateful,
                                      /*testonly_send_all=*/true);
  base::ScopedTempDir tmp_dir;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir);

  EXPECT_TRUE(collector.Collect(false /* is_mount_failure */));

  // Check report collection.
  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "umount_failure_stateful.*.meta", &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "umount_failure_stateful.*.log", &report_path));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("umount_failure_state\numount-encrypted-logs\n", report_contents);
  // Check meta contents
  std::string meta_contents;
  ASSERT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
  EXPECT_THAT(meta_contents, HasSubstr("upload_var_weight=10\n"));
}

TEST(MountFailureCollectorTest, TestCryptohomeMountFailure) {
  MountFailureCollectorMock collector(StorageDeviceType::kCryptohome,
                                      /*testonly_send_all=*/false);
  base::ScopedTempDir tmp_dir;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir);

  EXPECT_TRUE(collector.Collect(true /* is_mount_failure */));

  // Check report collection.
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      tmp_dir.GetPath(), "mount_failure_cryptohome.*.meta",
      "sig=mount_failure_cryptohome"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "mount_failure_cryptohome.*.log", &report_path));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("cryptohome\ndmesg\n", report_contents);
}

TEST(MountFailureCollectorTest, TestCryptohomeUmountFailure) {
  MountFailureCollectorMock collector(StorageDeviceType::kCryptohome,
                                      /*testonly_send_all=*/false);
  base::ScopedTempDir tmp_dir;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir);

  EXPECT_TRUE(collector.Collect(false /* is_mount_failure */));

  // Check report collection.
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      tmp_dir.GetPath(), "umount_failure_cryptohome.*.meta",
      "sig=umount_failure_cryptohome"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "umount_failure_cryptohome.*.log", &report_path));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("cryptohome\ndmesg\n", report_contents);
}

TEST(MountFailureCollectorTest, TestStatefulMountFailure_UploadWeightedUMA) {
  MountFailureCollectorMock collector(StorageDeviceType::kStateful,
                                      /*testonly_send_all=*/true);
  base::ScopedTempDir tmp_dir;
  auto metrics_lib = std::make_unique<MetricsLibraryMock>();
  MetricsLibraryMock* mock_ref = metrics_lib.get();
  collector.set_metrics_library_for_test(std::move(metrics_lib));
  EXPECT_CALL(*mock_ref,
              SendRepeatedEnumToUMA(
                  "ChromeOS.Stability.Warning",
                  static_cast<int>(CrashCollector::Product::kPlatform),
                  static_cast<int>(CrashCollector::Product::kMaxValue) + 1, 10))
      .WillOnce(Return(true));

  Initialize(&collector, &tmp_dir);

  EXPECT_TRUE(collector.Collect(false /* is_mount_failure */));
}

class MountFailureCollectorCrashSeverityTest
    : public ::testing::Test,
      public ::testing::WithParamInterface<
          test_util::ComputeCrashSeverityTestParams> {};

TEST_P(MountFailureCollectorCrashSeverityTest, ComputeCrashSeverity) {
  MountFailureCollectorMock collector(StorageDeviceType::kStateful,
                                      /*testonly_send_all=*/false);
  const test_util::ComputeCrashSeverityTestParams& test_case = GetParam();
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector.ComputeSeverity(test_case.exec_name);

  EXPECT_EQ(computed_severity.crash_severity, test_case.expected_severity);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

INSTANTIATE_TEST_SUITE_P(
    MountFailureCollectorCrashSeverityTestSuite,
    MountFailureCollectorCrashSeverityTest,
    testing::ValuesIn<test_util::ComputeCrashSeverityTestParams>({
        {"mount_failure_encstateful", CrashCollector::CrashSeverity::kFatal},
        {"mount_failure_stateful", CrashCollector::CrashSeverity::kFatal},
        {"umount_failure_stateful", CrashCollector::CrashSeverity::kWarning},
        {"another executable", CrashCollector::CrashSeverity::kUnspecified},
    }));

}  // namespace
