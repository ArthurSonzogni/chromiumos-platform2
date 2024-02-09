// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ramdisk_device.h"

#include <memory>

#include <brillo/blkdev_utils/loop_device_fake.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>
#include <linux/magic.h>

#include "cryptohome/storage/encrypted_container/backing_device.h"

namespace cryptohome {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {
constexpr char kBackingBase[] = "/mytmpfs";
constexpr char kBackingDir[] = "dir";
constexpr char kBackingFile[] = "file";
constexpr int kEphemeralfsFragmentSize = 1 << 10;
constexpr int kEphemeralfsSize = 1 << 12;
}  // namespace

class RamdiskDeviceTest : public ::testing::Test {
 public:
  RamdiskDeviceTest() { SetupFSMock(); }

 protected:
  void SetupFSMock() {
    ephemeral_statfs_ = {0};
    ephemeral_statfs_.f_type = TMPFS_MAGIC;
    ephemeral_statfs_.f_frsize = kEphemeralfsFragmentSize;
    ephemeral_statfs_.f_blocks = kEphemeralfsSize / kEphemeralfsFragmentSize;

    ON_CALL(platform_, StatFS(base::FilePath(kBackingBase), _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(ephemeral_statfs_), Return(true)));
  }

  NiceMock<libstorage::MockPlatform> platform_;
  struct statfs ephemeral_statfs_;
};

namespace {

TEST_F(RamdiskDeviceTest, Create_Success) {
  const base::FilePath ephemeral_root(kBackingBase);
  const base::FilePath ephemeral_sparse_file =
      ephemeral_root.Append(kBackingDir).Append(kBackingFile);

  ASSERT_TRUE(platform_.CreateDirectory(ephemeral_root));
  auto ramdisk = RamdiskDevice::Generate(ephemeral_sparse_file, &platform_);

  ASSERT_TRUE(ramdisk->Create());
  ASSERT_TRUE(platform_.FileExists(ephemeral_sparse_file));
  ASSERT_TRUE(ramdisk->Setup());
  ASSERT_TRUE(ramdisk->Teardown());
  ASSERT_FALSE(platform_.FileExists(ephemeral_sparse_file));
  ASSERT_TRUE(ramdisk->Purge());
  ASSERT_FALSE(platform_.FileExists(ephemeral_sparse_file));
}

TEST_F(RamdiskDeviceTest, Create_WrongFS) {
  const base::FilePath ephemeral_root(kBackingBase);
  const base::FilePath ephemeral_sparse_file =
      ephemeral_root.Append(kBackingDir).Append(kBackingFile);

  struct statfs ephemeral_statfs = {
      .f_type = EXT4_SUPER_MAGIC,
      .f_blocks = kEphemeralfsSize / kEphemeralfsFragmentSize,
      .f_frsize = kEphemeralfsFragmentSize,
  };
  EXPECT_CALL(platform_, StatFS(base::FilePath(kBackingBase), _))
      .WillOnce(DoAll(SetArgPointee<1>(ephemeral_statfs), Return(true)));
  EXPECT_FALSE(RamdiskDevice::Generate(ephemeral_sparse_file, &platform_));
}

TEST_F(RamdiskDeviceTest, Create_FailFS) {
  const base::FilePath ephemeral_sparse_file =
      base::FilePath(kBackingBase).Append(kBackingDir).Append(kBackingFile);

  EXPECT_CALL(platform_, StatFS(base::FilePath(kBackingBase), _))
      .WillOnce(Return(false));
  EXPECT_FALSE(RamdiskDevice::Generate(ephemeral_sparse_file, &platform_));
}

TEST_F(RamdiskDeviceTest, Create_FailDirCreation) {
  const base::FilePath ephemeral_root(kBackingBase);
  const base::FilePath ephemeral_sparse_file =
      ephemeral_root.Append(kBackingDir).Append(kBackingFile);

  ASSERT_TRUE(platform_.CreateDirectory(ephemeral_root));
  auto ramdisk = RamdiskDevice::Generate(ephemeral_sparse_file, &platform_);

  EXPECT_CALL(platform_, CreateDirectory(_)).WillOnce(Return(false));
  ASSERT_FALSE(ramdisk->Create());
  ASSERT_FALSE(platform_.FileExists(ephemeral_sparse_file));
}

}  // namespace

}  // namespace cryptohome
