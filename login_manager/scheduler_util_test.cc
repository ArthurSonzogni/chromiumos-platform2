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

constexpr const char* kHybridMaxFreqs[] = {
    "4400000", "4400000", "4400000", "4400000", "3300000", "3300000", "3300000",
    "3300000", "3300000", "3300000", "3300000", "3300000", "2100000", "2100000",
};
constexpr char kSmallCpuIdsFromHybridFreq[] = "10,11,12,13,4,5,6,7,8,9";

constexpr const char* kNonHybridMaxFreqs[] = {
    "4400000", "4400000", "4400000", "4400000",
    "4400000", "4400000", "4400000", "4400000",
};

constexpr const char* kCapacities[] = {
    "598",
    "598",
    "1024",
    "1024",
};
constexpr char kSmallCpuIdsFromCap[] = "0,1";

}  // namespace

using SchedulerUtilTest = ::testing::Test;

TEST_F(SchedulerUtilTest, TestSmallCoreCpuIdsFromCapacity) {
  base::ScopedTempDir tmpdir;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());
  base::FilePath test_dir = tmpdir.GetPath();

  int i = 0;
  for (const auto* capacity : kCapacities) {
    base::FilePath relative_path(
        base::StringPrintf("%s/cpu%d/%s", kCpuBusDir, i, kCpuCapFile));
    base::FilePath cap_path = test_dir.Append(relative_path);
    base::File::Error error;
    ASSERT_TRUE(base::CreateDirectoryAndGetError(cap_path.DirName(), &error))
        << "Error creating directory: " << error;
    ASSERT_TRUE(base::WriteFile(cap_path, capacity));
    i++;
  }

  std::vector<std::string> ecpu_ids = login_manager::GetSmallCoreCpuIdsFromAttr(
      test_dir.Append(kCpuBusDir), kCpuCapFile);
  EXPECT_TRUE(!ecpu_ids.empty());
  std::string ecpu_mask = base::JoinString(ecpu_ids, ",");
  EXPECT_EQ(ecpu_mask, kSmallCpuIdsFromCap);
}

TEST_F(SchedulerUtilTest, TestSmallCoreCpuIdsFromFreqForHybrid) {
  base::ScopedTempDir tmpdir;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());
  base::FilePath test_dir = tmpdir.GetPath();

  int i = 0;
  for (const auto* max_freq : kHybridMaxFreqs) {
    base::FilePath relative_path(
        base::StringPrintf("%s/cpu%d/%s", kCpuBusDir, i, kCpuMaxFreqFile));
    base::FilePath freq_path = test_dir.Append(relative_path);
    base::File::Error error;
    ASSERT_TRUE(base::CreateDirectoryAndGetError(freq_path.DirName(), &error))
        << "Error creating directory: " << error;
    ASSERT_TRUE(base::WriteFile(freq_path, max_freq));
    i++;
  }

  std::vector<std::string> ecpu_ids = login_manager::GetSmallCoreCpuIdsFromAttr(
      test_dir.Append(kCpuBusDir), kCpuMaxFreqFile);
  EXPECT_TRUE(!ecpu_ids.empty());
  std::string ecpu_mask = base::JoinString(ecpu_ids, ",");
  EXPECT_EQ(ecpu_mask, kSmallCpuIdsFromHybridFreq);
}

TEST_F(SchedulerUtilTest, TestSmallCoreCpuIdsFromFreqForNonHybrid) {
  base::ScopedTempDir tmpdir;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());
  base::FilePath test_dir = tmpdir.GetPath();

  int i = 0;
  for (const auto* max_freq : kNonHybridMaxFreqs) {
    base::FilePath relative_path(
        base::StringPrintf("%s/cpu%d/%s", kCpuBusDir, i, kCpuMaxFreqFile));
    base::FilePath freq_path = test_dir.Append(relative_path);
    base::File::Error error;
    ASSERT_TRUE(base::CreateDirectoryAndGetError(freq_path.DirName(), &error))
        << "Error creating directory: " << error;
    ASSERT_TRUE(base::WriteFile(freq_path, max_freq));
    i++;
  }

  std::vector<std::string> ecpu_ids = login_manager::GetSmallCoreCpuIdsFromAttr(
      test_dir.Append(kCpuBusDir), kCpuMaxFreqFile);
  EXPECT_TRUE(ecpu_ids.empty());
}

}  // namespace login_manager
