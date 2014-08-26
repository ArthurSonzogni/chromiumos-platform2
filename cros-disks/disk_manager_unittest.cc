// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk_manager.h"

#include <sys/mount.h>

#include <memory>

#include <base/file_util.h>
#include <base/files/file_path.h>
#include <base/stl_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/device_ejector.h"
#include "cros-disks/disk.h"
#include "cros-disks/exfat_mounter.h"
#include "cros-disks/external_mounter.h"
#include "cros-disks/filesystem.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mounter.h"
#include "cros-disks/ntfs_mounter.h"
#include "cros-disks/platform.h"

using std::map;
using std::string;
using std::unique_ptr;
using std::vector;
using testing::Return;
using testing::_;

namespace {

const char kMountRootDirectory[] = "/media/removable";

}  // namespace

namespace cros_disks {

// A mock device ejector class for testing the disk manager class.
class MockDeviceEjector : public DeviceEjector {
 public:
  MockDeviceEjector() {}

  MOCK_METHOD1(Eject, bool(const string& device_path));
};

class DiskManagerTest : public ::testing::Test {
 public:
  DiskManagerTest()
      : manager_(kMountRootDirectory, &platform_, &metrics_, &device_ejector_) {
  }

 protected:
  Metrics metrics_;
  Platform platform_;
  MockDeviceEjector device_ejector_;
  DiskManager manager_;
};

TEST_F(DiskManagerTest, CreateExFATMounter) {
  Disk disk;
  disk.set_device_file("/dev/sda1");

  Filesystem filesystem("exfat");
  filesystem.set_mounter_type(ExFATMounter::kMounterType);

  string target_path = "/media/disk";
  vector<string> options = {"rw", "nodev", "noexec", "nosuid"};

  unique_ptr<Mounter> mounter(
      manager_.CreateMounter(disk, filesystem, target_path, options));
  EXPECT_TRUE(mounter.get() != NULL);
  EXPECT_EQ(filesystem.mount_type(), mounter->filesystem_type());
  EXPECT_EQ(disk.device_file(), mounter->source_path());
  EXPECT_EQ(target_path, mounter->target_path());
  EXPECT_EQ("rw,nodev,noexec,nosuid", mounter->mount_options().ToString());
}

TEST_F(DiskManagerTest, CreateExternalMounter) {
  Disk disk;
  disk.set_device_file("/dev/sda1");

  Filesystem filesystem("fat");
  filesystem.set_mount_type("vfat");
  filesystem.set_mounter_type(ExternalMounter::kMounterType);

  string target_path = "/media/disk";
  vector<string> options = {"rw", "nodev", "noexec", "nosuid"};

  unique_ptr<Mounter> mounter(
      manager_.CreateMounter(disk, filesystem, target_path, options));
  EXPECT_TRUE(mounter.get() != NULL);
  EXPECT_EQ(filesystem.mount_type(), mounter->filesystem_type());
  EXPECT_EQ(disk.device_file(), mounter->source_path());
  EXPECT_EQ(target_path, mounter->target_path());
  EXPECT_EQ("rw,nodev,noexec,nosuid", mounter->mount_options().ToString());
}

TEST_F(DiskManagerTest, CreateNTFSMounter) {
  Disk disk;
  disk.set_device_file("/dev/sda1");

  Filesystem filesystem("ntfs");
  filesystem.set_mounter_type(NTFSMounter::kMounterType);

  string target_path = "/media/disk";
  vector<string> options = {"rw", "nodev", "noexec", "nosuid"};

  unique_ptr<Mounter> mounter(
      manager_.CreateMounter(disk, filesystem, target_path, options));
  EXPECT_TRUE(mounter.get() != NULL);
  EXPECT_EQ(filesystem.mount_type(), mounter->filesystem_type());
  EXPECT_EQ(disk.device_file(), mounter->source_path());
  EXPECT_EQ(target_path, mounter->target_path());
  EXPECT_EQ("rw,nodev,noexec,nosuid", mounter->mount_options().ToString());
}

TEST_F(DiskManagerTest, CreateSystemMounter) {
  Disk disk;
  disk.set_device_file("/dev/sda1");

  Filesystem filesystem("vfat");
  filesystem.AddExtraMountOption("utf8");
  filesystem.AddExtraMountOption("shortname=mixed");

  string target_path = "/media/disk";
  vector<string> options = {"rw", "nodev", "noexec", "nosuid"};

  unique_ptr<Mounter> mounter(
      manager_.CreateMounter(disk, filesystem, target_path, options));
  EXPECT_TRUE(mounter.get() != NULL);
  EXPECT_EQ(filesystem.mount_type(), mounter->filesystem_type());
  EXPECT_EQ(disk.device_file(), mounter->source_path());
  EXPECT_EQ(target_path, mounter->target_path());
  EXPECT_EQ("utf8,shortname=mixed,rw,nodev,noexec,nosuid",
            mounter->mount_options().ToString());
}

TEST_F(DiskManagerTest, EnumerateDisks) {
  vector<Disk> disks = manager_.EnumerateDisks();
}

TEST_F(DiskManagerTest, GetDiskByDevicePath) {
  Disk disk;
  string device_path = "/dev/sda";
  EXPECT_TRUE(manager_.GetDiskByDevicePath(device_path, &disk));
  EXPECT_EQ(device_path, disk.device_file());

  device_path = "/dev/sda1";
  EXPECT_TRUE(manager_.GetDiskByDevicePath(device_path, &disk));
  EXPECT_EQ(device_path, disk.device_file());
}

TEST_F(DiskManagerTest, GetDiskByNonexistentDevicePath) {
  Disk disk;
  string device_path = "/dev/nonexistent-path";
  EXPECT_FALSE(manager_.GetDiskByDevicePath(device_path, &disk));
}

TEST_F(DiskManagerTest, GetFilesystem) {
  const Filesystem* null_pointer = NULL;

  EXPECT_EQ(null_pointer, manager_.GetFilesystem("nonexistent-fs"));

  Filesystem normal_fs("normal-fs");
  EXPECT_EQ(null_pointer, manager_.GetFilesystem(normal_fs.type()));
  manager_.RegisterFilesystem(normal_fs);
  EXPECT_NE(null_pointer, manager_.GetFilesystem(normal_fs.type()));
}

TEST_F(DiskManagerTest, RegisterFilesystem) {
  map<string, Filesystem>& filesystems = manager_.filesystems_;
  EXPECT_EQ(0, filesystems.size());
  EXPECT_TRUE(filesystems.find("nonexistent") == filesystems.end());

  Filesystem fat_fs("fat");
  fat_fs.set_accepts_user_and_group_id(true);
  manager_.RegisterFilesystem(fat_fs);
  EXPECT_EQ(1, filesystems.size());
  EXPECT_TRUE(filesystems.find(fat_fs.type()) != filesystems.end());

  Filesystem vfat_fs("vfat");
  vfat_fs.set_accepts_user_and_group_id(true);
  manager_.RegisterFilesystem(vfat_fs);
  EXPECT_EQ(2, filesystems.size());
  EXPECT_TRUE(filesystems.find(vfat_fs.type()) != filesystems.end());
}

TEST_F(DiskManagerTest, CanMount) {
  EXPECT_TRUE(manager_.CanMount("/dev/sda1"));
  EXPECT_TRUE(manager_.CanMount("/devices/block/sda/sda1"));
  EXPECT_TRUE(manager_.CanMount("/sys/devices/block/sda/sda1"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk 1"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/test.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/test.zip/"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/test 1.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/test.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/test 1.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/dir1/test.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/test.zip/test1.zip"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/Downloads/test1.zip"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/GCache/test1.zip"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos"
                                 "/u-0123456789abcdef0123456789abcdef01234567"
                                 "/Downloads/test1.zip"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos"
                                 "/u-0123456789abcdef0123456789abcdef01234567"
                                 "/GCache/test1.zip"));
  EXPECT_FALSE(manager_.CanMount(""));
  EXPECT_FALSE(manager_.CanMount("/tmp"));
  EXPECT_FALSE(manager_.CanMount("/media/removable"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/"));
  EXPECT_FALSE(manager_.CanMount("/media/archive"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/Downloads"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/Downloads/"));
}

TEST_F(DiskManagerTest, CanUnmount) {
  EXPECT_TRUE(manager_.CanUnmount("/dev/sda1"));
  EXPECT_TRUE(manager_.CanUnmount("/devices/block/sda/sda1"));
  EXPECT_TRUE(manager_.CanUnmount("/sys/devices/block/sda/sda1"));
  EXPECT_TRUE(manager_.CanUnmount("/media/removable/disk1"));
  EXPECT_TRUE(manager_.CanUnmount("/media/removable/disk1/"));
  EXPECT_TRUE(manager_.CanUnmount("/media/removable/disk 1"));
  EXPECT_FALSE(manager_.CanUnmount("/media/archive/test.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/media/archive/test.zip/"));
  EXPECT_FALSE(manager_.CanUnmount("/media/archive/test 1.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/media/removable/disk1/test.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/media/removable/disk1/test 1.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/media/removable/disk1/dir1/test.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/media/removable/test.zip/test1.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/home/chronos/user/Downloads/test1.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/home/chronos/user/GCache/test1.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/home/chronos"
                                   "/u-0123456789abcdef0123456789abcdef01234567"
                                   "/Downloads/test1.zip"));
  EXPECT_FALSE(manager_.CanUnmount("/home/chronos"
                                   "/u-0123456789abcdef0123456789abcdef01234567"
                                   "/GCache/test1.zip"));
  EXPECT_FALSE(manager_.CanUnmount(""));
  EXPECT_FALSE(manager_.CanUnmount("/tmp"));
  EXPECT_FALSE(manager_.CanUnmount("/media/removable"));
  EXPECT_FALSE(manager_.CanUnmount("/media/removable/"));
  EXPECT_FALSE(manager_.CanUnmount("/media/archive"));
  EXPECT_FALSE(manager_.CanUnmount("/media/archive/"));
  EXPECT_FALSE(manager_.CanUnmount("/home/chronos/user/Downloads"));
  EXPECT_FALSE(manager_.CanUnmount("/home/chronos/user/Downloads/"));
}

TEST_F(DiskManagerTest, DoMountDiskWithNonexistentSourcePath) {
  string filesystem_type = "ext3";
  string source_path = "/dev/nonexistent-path";
  string mount_path = "/tmp/cros-disks-test";
  vector<string> options;
  EXPECT_EQ(MOUNT_ERROR_INVALID_DEVICE_PATH,
            manager_.DoMount(source_path, filesystem_type, options,
                             mount_path));
}

TEST_F(DiskManagerTest, DoUnmountDiskWithInvalidUnmountOptions) {
  string source_path = "/dev/nonexistent-path";
  vector<string> options = {"invalid-unmount-option"};
  EXPECT_EQ(MOUNT_ERROR_INVALID_UNMOUNT_OPTIONS,
            manager_.DoUnmount(source_path, options));
}

TEST_F(DiskManagerTest, ScheduleEjectOnUnmount) {
  string mount_path = "/media/removable/disk";
  Disk disk;
  disk.set_device_file("/dev/sr0");
  EXPECT_FALSE(manager_.ScheduleEjectOnUnmount(mount_path, disk));
  EXPECT_FALSE(ContainsKey(manager_.devices_to_eject_on_unmount_, mount_path));

  disk.set_media_type(DEVICE_MEDIA_OPTICAL_DISC);
  EXPECT_TRUE(manager_.ScheduleEjectOnUnmount(mount_path, disk));
  EXPECT_TRUE(ContainsKey(manager_.devices_to_eject_on_unmount_, mount_path));

  disk.set_media_type(DEVICE_MEDIA_DVD);
  manager_.devices_to_eject_on_unmount_.clear();
  EXPECT_TRUE(manager_.ScheduleEjectOnUnmount(mount_path, disk));
  EXPECT_TRUE(ContainsKey(manager_.devices_to_eject_on_unmount_, mount_path));
}

TEST_F(DiskManagerTest, EjectDeviceOfMountPath) {
  string mount_path = "/media/removable/disk";
  string device_file = "/dev/sr0";
  manager_.devices_to_eject_on_unmount_[mount_path] = device_file;
  EXPECT_CALL(device_ejector_, Eject(_)).WillOnce(Return(true));
  EXPECT_TRUE(manager_.EjectDeviceOfMountPath(mount_path));
  EXPECT_FALSE(ContainsKey(manager_.devices_to_eject_on_unmount_, mount_path));
}

TEST_F(DiskManagerTest, EjectDeviceOfMountPathWhenEjectFailed) {
  string mount_path = "/media/removable/disk";
  string device_file = "/dev/sr0";
  manager_.devices_to_eject_on_unmount_[mount_path] = device_file;
  EXPECT_CALL(device_ejector_, Eject(_)).WillOnce(Return(false));
  EXPECT_FALSE(manager_.EjectDeviceOfMountPath(mount_path));
  EXPECT_FALSE(ContainsKey(manager_.devices_to_eject_on_unmount_, mount_path));
}

TEST_F(DiskManagerTest, EjectDeviceOfMountPathWhenExplicitlyDisabled) {
  string mount_path = "/media/removable/disk";
  string device_file = "/dev/sr0";
  manager_.devices_to_eject_on_unmount_[mount_path] = device_file;
  manager_.eject_device_on_unmount_ = false;
  EXPECT_CALL(device_ejector_, Eject(_)).Times(0);
  EXPECT_FALSE(manager_.EjectDeviceOfMountPath(mount_path));
  EXPECT_FALSE(ContainsKey(manager_.devices_to_eject_on_unmount_, mount_path));
}

TEST_F(DiskManagerTest, EjectDeviceOfMountPathWhenMountPathExcluded) {
  EXPECT_CALL(device_ejector_, Eject(_)).Times(0);
  EXPECT_FALSE(manager_.EjectDeviceOfMountPath("/media/removable/disk"));
}

}  // namespace cros_disks
