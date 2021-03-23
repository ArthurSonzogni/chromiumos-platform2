// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

const char kFakeMountSource[] = "/dev/mmcblk0p1";
const char kFakeFilesystem[] = "ext4";
const char kFakeMtabOpt[] = "rw 0 0";

}  // namespace

class StatefulePartitionFetcherTest : public ::testing::Test {
 protected:
  StatefulePartitionFetcherTest() = default;
  StatefulePartitionFetcherTest(const StatefulePartitionFetcherTest&) = delete;
  StatefulePartitionFetcherTest& operator=(
      const StatefulePartitionFetcherTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    root_dir_ = GetTempDirPath();
    // Populate fake stateful partition directory.
    const auto stateful_partition_dir =
        root_dir_.Append(kStatefulPartitionPath);
    ASSERT_TRUE(base::CreateDirectory(stateful_partition_dir));
    // Populate fake mtab contents.
    const auto mtab_path = root_dir_.Append(kMtabPath);
    const auto fake_content = std::string(kFakeMountSource) + " " +
                              stateful_partition_dir.value() + " " +
                              kFakeFilesystem + " " + kFakeMtabOpt;
    ASSERT_TRUE(WriteFileAndCreateParentDirs(mtab_path, fake_content));
  }

  const base::FilePath& GetTempDirPath() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  const base::FilePath& root_dir() { return root_dir_; }

 private:
  base::ScopedTempDir temp_dir_;
  base::FilePath root_dir_;
};

TEST_F(StatefulePartitionFetcherTest, TestFetchStatefulPartitionInfo) {
  const auto result = FetchStatefulPartitionInfo(root_dir());
  ASSERT_TRUE(result->is_partition_info());
  EXPECT_GE(result->get_partition_info()->available_space, 0);
  EXPECT_EQ(result->get_partition_info()->filesystem, kFakeFilesystem);
  EXPECT_EQ(result->get_partition_info()->mount_source, kFakeMountSource);
}

TEST_F(StatefulePartitionFetcherTest, TestNoStatefulPartition) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kStatefulPartitionPath)));

  const auto result = FetchStatefulPartitionInfo(root_dir());
  EXPECT_TRUE(result->is_error());
}

TEST_F(StatefulePartitionFetcherTest, TestNoMtabFile) {
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kMtabPath)));

  const auto result = FetchStatefulPartitionInfo(root_dir());
  EXPECT_TRUE(result->is_error());
}

}  // namespace diagnostics
