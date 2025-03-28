// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/ec_collector.h"

#include <memory>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/test_util.h"

using base::FilePath;
using brillo::FindLog;

namespace {

const char kECPanicInfo[] = "panicinfo";
const char kDevCoredumpDirectory[] = "cros_ec";
const unsigned int kPanicDataArchByte = 0;
const unsigned int kPanicDataVersionByte = 1;
const unsigned int kPanicDataReservedByte = 3;

}  // namespace

class ECCollectorMock : public ECCollector {
 public:
  ECCollectorMock()
      : ECCollector(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));
  MOCK_METHOD(int,
              RunEctoolCmd,
              (const std::vector<std::string>&, std::string*),
              (override));
};

class ECCollectorTest : public ::testing::Test {
 protected:
  void PreparePanicInfo(bool present, bool stale, bool valid = true) {
    FilePath panicinfo_path = collector_.debugfs_path_.Append(kECPanicInfo);

    if (present) {
      char data[116];
      for (unsigned int i = 0; i < sizeof(data); i++) {
        data[i] = i;
      }

      if (valid) {
        // Forge a panic data with valid arch, struct version, flag, and
        // reserved.
        data[kPanicDataArchByte] = 1;
        data[kPanicDataVersionByte] = 2;
        data[PANIC_DATA_FLAGS_BYTE] = 0;
        data[kPanicDataReservedByte] = 0;
      }
      if (stale) {
        data[PANIC_DATA_FLAGS_BYTE] |= PANIC_DATA_FLAG_OLD_HOSTCMD;
      } else {
        data[PANIC_DATA_FLAGS_BYTE] &= ~PANIC_DATA_FLAG_OLD_HOSTCMD;
      }

      ASSERT_TRUE(base::WriteFile(panicinfo_path,
                                  std::string_view(data, sizeof(data))));
    } else {
      base::DeleteFile(panicinfo_path);
    }
  }

  base::ScopedTempDir temp_dir_generator_;

  ECCollectorMock collector_;

 private:
  void SetUp() override {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(false);

    ASSERT_TRUE(temp_dir_generator_.CreateUniqueTempDir());

    collector_.set_crash_directory_for_test(temp_dir_generator_.GetPath());

    FilePath debugfs_path =
        temp_dir_generator_.GetPath().Append(kDevCoredumpDirectory);
    ASSERT_TRUE(base::CreateDirectory(debugfs_path));
    collector_.debugfs_path_ = debugfs_path;
  }
};

TEST_F(ECCollectorTest, TestNoCrash) {
  PreparePanicInfo(false, false);
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}

TEST_F(ECCollectorTest, TestStale) {
  PreparePanicInfo(true, true);
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Stale EC crash"));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}

TEST_F(ECCollectorTest, TestGood) {
  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("(handling)"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      temp_dir_generator_.GetPath(), "embedded_controller.*.meta",
      "upload_var_collector=ec"));
  /* TODO(drinkcat): Test crash file content */
}

TEST_F(ECCollectorTest, TestInvalid) {
  PreparePanicInfo(true, false, false);
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
}

TEST_F(ECCollectorTest, TestGetCreatedCrashDirectoryByEuidFailure) {
  PreparePanicInfo(true, false);
  collector_.force_get_created_crash_directory_by_euid_status_for_test(
      CrashCollectionStatus::kOutOfCapacity, true);

  // This failure returns true but doesn't create the file.
  EXPECT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      temp_dir_generator_.GetPath(), "*.meta", nullptr));
}

TEST_F(ECCollectorTest, ComputeSeverity) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("any executable");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kFatal);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

TEST_F(ECCollectorTest, PanicLogValid) {
  std::string panic_log_dump = __func__;
  std::string pnaic_log_info_output = std::format(
      "valid: 1\n"
      "frozen: 1\n"
      "length: {}\n",
      panic_log_dump.length());

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "info"}, testing::_))
      .WillOnce(testing::DoAll(testing::SetArgPointee<1>(pnaic_log_info_output),
                               testing::Return(0)));

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "dump"}, testing::_))
      .WillOnce(testing::DoAll(testing::SetArgPointee<1>(panic_log_dump),
                               testing::Return(0)));

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "reset"}, testing::_))
      .WillOnce(testing::Return(0));

  EXPECT_CALL(collector_,
              RunEctoolCmd(std::vector<std::string>{"paniclog", "unfreeze"},
                           testing::_))
      .WillOnce(testing::Return(0));

  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      temp_dir_generator_.GetPath(), "embedded_controller.*.meta",
      "upload_file_panic_log="));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      temp_dir_generator_.GetPath(), "embedded_controller.*.panic.log",
      panic_log_dump));
}

TEST_F(ECCollectorTest, PanicLogInvalid) {
  std::string panic_log_dump = __func__;
  std::string pnaic_log_info_output = std::format(
      "valid: 0\n"
      "frozen: 1\n"
      "length: {}\n",
      panic_log_dump.length());

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "info"}, testing::_))
      .WillOnce(testing::DoAll(testing::SetArgPointee<1>(pnaic_log_info_output),
                               testing::Return(0)));

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "reset"}, testing::_))
      .WillOnce(testing::Return(0));

  EXPECT_CALL(collector_,
              RunEctoolCmd(std::vector<std::string>{"paniclog", "unfreeze"},
                           testing::_))
      .WillOnce(testing::Return(0));

  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("not valid"));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      temp_dir_generator_.GetPath(), "embedded_controller.*.panic.log",
      nullptr));
}

TEST_F(ECCollectorTest, PanicLogNotSupported) {
  EXPECT_CALL(collector_, RunEctoolCmd(testing::_, testing::_))
      .WillOnce(testing::Return(1));

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "reset"}, testing::_))
      .Times(testing::Exactly(0));

  EXPECT_CALL(collector_,
              RunEctoolCmd(std::vector<std::string>{"paniclog", "unfreeze"},
                           testing::_))
      .Times(testing::Exactly(0));

  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("not supported"));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      temp_dir_generator_.GetPath(), "embedded_controller.*.panic.log",
      nullptr));
}

TEST_F(ECCollectorTest, PanicLogEmpty) {
  std::string panic_log_dump = __func__;
  std::string pnaic_log_info_output = std::format(
      "valid: 0\n"
      "frozen: 1\n"
      "length: {}\n",
      panic_log_dump.length());

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "info"}, testing::_))
      .WillOnce(testing::DoAll(testing::Return(0)));

  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("empty"));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      temp_dir_generator_.GetPath(), "embedded_controller.*.panic.log",
      nullptr));
}

TEST_F(ECCollectorTest, PanicLogNotAvailable) {
  std::string pnaic_log_info_output = std::format(
      "valid: 1\n"
      "frozen: 0\n"
      "length: 0\n");

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "info"}, testing::_))
      .WillOnce(testing::DoAll(testing::SetArgPointee<1>(pnaic_log_info_output),
                               testing::Return(0)));

  EXPECT_CALL(
      collector_,
      RunEctoolCmd(std::vector<std::string>{"paniclog", "reset"}, testing::_))
      .Times(testing::Exactly(0));

  EXPECT_CALL(collector_,
              RunEctoolCmd(std::vector<std::string>{"paniclog", "unfreeze"},
                           testing::_))
      .Times(testing::Exactly(0));

  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("not available"));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      temp_dir_generator_.GetPath(), "embedded_controller.*.panic.log",
      nullptr));
}

class ECCollectorSavedLsbTest : public ECCollectorTest,
                                public ::testing::WithParamInterface<bool> {};

TEST_P(ECCollectorSavedLsbTest, UsesSavedLsb) {
  FilePath lsb_release = temp_dir_generator_.GetPath().Append("lsb-release");
  collector_.set_lsb_release_for_test(lsb_release);
  const char kLsbContents[] =
      "CHROMEOS_RELEASE_BOARD=lumpy\n"
      "CHROMEOS_RELEASE_VERSION=6727.0.2015_01_26_0853\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_CHROME_MILESTONE=82\n"
      "CHROMEOS_RELEASE_TRACK=testimage-channel\n"
      "CHROMEOS_RELEASE_DESCRIPTION=6727.0.2015_01_26_0853 (Test Build - foo)";
  ASSERT_TRUE(test_util::CreateFile(lsb_release, kLsbContents));

  FilePath saved_lsb_dir =
      temp_dir_generator_.GetPath().Append("crash-reporter-state");
  ASSERT_TRUE(base::CreateDirectory(saved_lsb_dir));
  collector_.set_reporter_state_directory_for_test(saved_lsb_dir);

  const char kSavedLsbContents[] =
      "CHROMEOS_RELEASE_BOARD=lumpy\n"
      "CHROMEOS_RELEASE_VERSION=12345.0.2015_01_26_0853\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_CHROME_MILESTONE=81\n"
      "CHROMEOS_RELEASE_TRACK=beta-channel\n"
      "CHROMEOS_RELEASE_DESCRIPTION=12345.0.2015_01_26_0853 (Test Build - foo)";
  base::FilePath saved_lsb = saved_lsb_dir.Append("lsb-release");
  ASSERT_TRUE(test_util::CreateFile(saved_lsb, kSavedLsbContents));

  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/GetParam()));

  if (GetParam()) {
    EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
        temp_dir_generator_.GetPath(), "*.meta",
        "ver=12345.0.2015_01_26_0853\n"));
  } else {
    EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
        temp_dir_generator_.GetPath(), "*.meta",
        "ver=6727.0.2015_01_26_0853\n"));
  }
}

INSTANTIATE_TEST_SUITE_P(ECCollectorSavedLsbTest,
                         ECCollectorSavedLsbTest,
                         testing::Bool());
