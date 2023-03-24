// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/scheduler_util.h"

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <gtest/gtest.h>

namespace login_manager {

namespace {

constexpr char kCpuBusDir[] = "sys/bus/cpu/devices";
constexpr char kCpuCapFile[] = "cpu_capacity";
constexpr char kCpuMaxFreqFile[] = "cpufreq/cpuinfo_max_freq";
constexpr char kUseFlagsFile[] = "etc/ui_use_flags.txt";

constexpr const char* kMaxFreqs[] = {
    "4400000", "4400000", "4400000", "4400000",
    "3300000", "3300000", "3300000", "3300000",
};
constexpr char kSmallCpuIdsFromFreq[] = "4,5,6,7";

constexpr const char* kCapacities[] = {
    "598",
    "598",
    "1024",
    "1024",
};
constexpr char kSmallCpuIdsFromCap[] = "0,1";

}  // namespace

class SchedulerUtilTest : public ::testing::Test {
 public:
  SchedulerUtilTest() = default;
  SchedulerUtilTest(const SchedulerUtilTest&) = delete;
  SchedulerUtilTest& operator=(const SchedulerUtilTest&) = delete;
  ~SchedulerUtilTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    test_dir_ = tmpdir_.GetPath();
  }

 protected:
  base::ScopedTempDir tmpdir_;
  base::FilePath test_dir_;
};

TEST_F(SchedulerUtilTest, VerifyHybridFlag) {
  base::FilePath use_flags_path = test_dir_.Append(kUseFlagsFile);
  base::File::Error error;
  ASSERT_TRUE(
      base::CreateDirectoryAndGetError(use_flags_path.DirName(), &error))
      << "Error creating directory: " << error;
  constexpr char kContent[] =
      "# This file is just for libchrome's ChromiumCommandBuilder class.\n"
      "# Don't use it for anything else. Your code will break.\n"
      "big_little\n"
      "biod\n"
      "compupdates\n"
      "diagnostics\n"
      "drm_atomic\n";
  ASSERT_TRUE(base::WriteFile(use_flags_path, kContent));

  EXPECT_TRUE(login_manager::HasHybridFlag(use_flags_path));
}

TEST_F(SchedulerUtilTest, TestSmallCoreCpuIdsFromCapacity) {
  int i = 0;
  for (const auto* capacity : kCapacities) {
    base::FilePath relative_path(
        base::StringPrintf("%s/cpu%d/%s", kCpuBusDir, i, kCpuCapFile));
    base::FilePath cap_path = test_dir_.Append(relative_path);
    base::File::Error error;
    ASSERT_TRUE(base::CreateDirectoryAndGetError(cap_path.DirName(), &error))
        << "Error creating directory: " << error;
    ASSERT_TRUE(base::WriteFile(cap_path, capacity));
    i++;
  }

  std::vector<std::string> ecpu_ids = login_manager::GetSmallCoreCpuIdsFromAttr(
      test_dir_.Append(kCpuBusDir), kCpuCapFile);
  EXPECT_TRUE(!ecpu_ids.empty());
  std::string ecpu_mask = base::JoinString(ecpu_ids, ",");
  EXPECT_EQ(ecpu_mask, kSmallCpuIdsFromCap);
}

TEST_F(SchedulerUtilTest, TestSmallCoreCpuIdsFromFreq) {
  int i = 0;
  for (const auto* max_freq : kMaxFreqs) {
    base::FilePath relative_path(
        base::StringPrintf("%s/cpu%d/%s", kCpuBusDir, i, kCpuMaxFreqFile));
    base::FilePath freq_path = test_dir_.Append(relative_path);
    base::File::Error error;
    ASSERT_TRUE(base::CreateDirectoryAndGetError(freq_path.DirName(), &error))
        << "Error creating directory: " << error;
    ASSERT_TRUE(base::WriteFile(freq_path, max_freq));
    i++;
  }

  std::vector<std::string> ecpu_ids = login_manager::GetSmallCoreCpuIdsFromAttr(
      test_dir_.Append(kCpuBusDir), kCpuMaxFreqFile);
  EXPECT_TRUE(!ecpu_ids.empty());
  std::string ecpu_mask = base::JoinString(ecpu_ids, ",");
  EXPECT_EQ(ecpu_mask, kSmallCpuIdsFromFreq);
}

}  // namespace login_manager
