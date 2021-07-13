// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/mock_platform.h"
#include "cros-disks/platform.h"

namespace cros_disks {

namespace {

using testing::_;
using testing::Return;

constexpr char kMountPath[] = "/mount/path";
constexpr char kSource[] = "source";
constexpr char kFSType[] = "fstype";
constexpr char kOptions[] = "foo=bar";

}  // namespace

TEST(MountPointTest, Unmount) {
  MockPlatform platform;
  MountPointData data = {
      .mount_path = base::FilePath(kMountPath),
      .source = kSource,
      .filesystem_type = kFSType,
      .flags = MS_DIRSYNC | MS_NODEV,
      .data = kOptions,
  };
  auto mount_point = std::make_unique<MountPoint>(std::move(data), &platform);

  EXPECT_CALL(platform, Unmount(kMountPath, _))
      .WillOnce(Return(MOUNT_ERROR_INVALID_ARCHIVE))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_EQ(MOUNT_ERROR_INVALID_ARCHIVE, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());
}

TEST(MountPointTest, UnmountBusy) {
  MockPlatform platform;
  MountPointData data = {
      .mount_path = base::FilePath(kMountPath),
      .source = kSource,
      .filesystem_type = kFSType,
      .flags = MS_DIRSYNC | MS_NODEV,
      .data = kOptions,
  };
  auto mount_point = std::make_unique<MountPoint>(std::move(data), &platform);

  // Unmount will retry unmounting with force and detach if mount point busy.
  EXPECT_CALL(platform, Unmount(kMountPath, 0))
      .WillOnce(Return(MOUNT_ERROR_PATH_ALREADY_MOUNTED));
  EXPECT_CALL(platform, Unmount(kMountPath, MNT_DETACH | MNT_FORCE))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());
}

TEST(MountPointTest, UnmountOnDestroy) {
  MockPlatform platform;
  MountPointData data = {
      .mount_path = base::FilePath(kMountPath),
      .source = kSource,
      .filesystem_type = kFSType,
      .flags = MS_DIRSYNC | MS_NODEV,
      .data = kOptions,
  };
  auto mount_point = std::make_unique<MountPoint>(std::move(data), &platform);

  EXPECT_CALL(platform, Unmount).WillOnce(Return(MOUNT_ERROR_INVALID_ARCHIVE));
  mount_point.reset();
}

TEST(MountPointTest, Remount) {
  MockPlatform platform;
  MountPointData data = {
      .mount_path = base::FilePath(kMountPath),
      .source = kSource,
      .filesystem_type = kFSType,
      .flags = MS_DIRSYNC | MS_NODEV,
      .data = kOptions,
  };
  auto mount_point = std::make_unique<MountPoint>(std::move(data), &platform);
  EXPECT_FALSE(mount_point->is_read_only());

  EXPECT_CALL(platform,
              Mount(kSource, kMountPath, kFSType,
                    MS_DIRSYNC | MS_NODEV | MS_RDONLY | MS_REMOUNT, kOptions))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Remount(true));
  EXPECT_TRUE(mount_point->is_read_only());

  EXPECT_CALL(platform, Mount(kSource, kMountPath, kFSType,
                              MS_DIRSYNC | MS_NODEV | MS_REMOUNT, kOptions))
      .WillOnce(Return(MOUNT_ERROR_INTERNAL));
  EXPECT_EQ(MOUNT_ERROR_INTERNAL, mount_point->Remount(false));
  EXPECT_TRUE(mount_point->is_read_only());
}

TEST(MountPointTest, Mount) {
  MockPlatform platform;
  MountPointData data = {
      .mount_path = base::FilePath(kMountPath),
      .source = kSource,
      .filesystem_type = kFSType,
      .flags = MS_DIRSYNC | MS_NODEV,
      .data = kOptions,
  };
  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  EXPECT_CALL(platform, Mount(kSource, kMountPath, kFSType,
                              MS_DIRSYNC | MS_NODEV, kOptions))
      .WillOnce(Return(MOUNT_ERROR_INVALID_ARGUMENT))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto mount_point = MountPoint::Mount(data, &platform, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_INVALID_ARGUMENT, error);
  mount_point = MountPoint::Mount(data, &platform, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
  EXPECT_EQ(kMountPath, mount_point->path().value());

  mount_point->Release();
}

TEST(MountPointTest, Release) {
  MockPlatform platform;
  MountPointData data = {
      .mount_path = base::FilePath(kMountPath),
      .source = kSource,
      .filesystem_type = kFSType,
      .flags = MS_DIRSYNC | MS_NODEV,
      .data = kOptions,
  };
  auto mount_point = std::make_unique<MountPoint>(std::move(data), &platform);

  EXPECT_CALL(platform, Mount).Times(0);
  EXPECT_CALL(platform, Unmount).Times(0);
  mount_point->Release();
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Remount(true));
}

}  // namespace cros_disks
