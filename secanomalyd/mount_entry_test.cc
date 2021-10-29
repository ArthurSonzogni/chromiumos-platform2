// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for MountEntry.

#include "secanomalyd/mount_entry.h"

#include <gtest/gtest.h>

namespace secanomalyd {

TEST(MountEntryTest, RoRootFs) {
  MountEntry e("/dev/root / ext2 ro,seclabel,relatime 0 0");
  ASSERT_FALSE(e.IsWX());
}

TEST(MountEntryTest, WNoExecStateful) {
  MountEntry e(
      "/dev/sda1 /mnt/stateful_partition ext4 "
      "rw,seclabel,nosuid,nodev,noexec,noatime,"
      "resgid=20119,commit=600,data=ordered 0 0");
  ASSERT_FALSE(e.IsWX());
}

TEST(MountEntryTest, WXUsrLocal) {
  MountEntry e(
      "/dev/sda1 /usr/local ext4 "
      "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0");
  ASSERT_TRUE(e.IsWX());
}

TEST(MountEntryTest, MisplacedRW) {
  MountEntry e("/dev/sda1 /pointless_path ext4 ro,nodev,somethingrw");
  ASSERT_FALSE(e.IsWX());
}

TEST(MountEntryTest, MisplacedNoExec) {
  MountEntry e("/dev/sda1 /pointless_path ext4 rw,nodev,notreallynoexec");
  ASSERT_TRUE(e.IsWX());
}

TEST(MountEntryTest, UsbDrive) {
  MountEntry e(
      "/dev/sdb1 /media/removable/USB\040Drive ext2 "
      "rw,dirsync,nosuid,nodev,noexec,seclabel,relatime,nosymfollow");
  ASSERT_TRUE(e.IsUsbDriveOrArchive());
}

TEST(MountEntryTest, ArchiveFileInDownloads) {
  MountEntry e(
      "fuse:/home/chronos/u-f0df208cd7759644d43f8d7c4c5900e4a4875275/MyFiles/"
      "Downloads/sample.rar /media/archive/sample.rar fuse.rarfs "
      "ro,dirsync,nosuid,nodev,noexec,relatime,nosymfollow,"
      "user_id=1000,group_id=1001,default_permissions,allow_other 0 0");
  ASSERT_TRUE(e.IsUsbDriveOrArchive());
}

TEST(MountEntryTest, UsrLocalIsUsrLocal) {
  MountEntry e(
      "/dev/sda1 /usr/local ext4 "
      "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0");
  ASSERT_TRUE(e.IsDestInUsrLocal());
}

TEST(MountEntryTest, UsrLocalBinIsInUsrLocal) {
  MountEntry e(
      "/dev/sda1 /usr/local/bin ext4 "
      "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0");
  ASSERT_TRUE(e.IsDestInUsrLocal());
}

TEST(MountEntryTest, UsrIsNotInUsrLocal) {
  MountEntry e(
      "/dev/sda1 /usr ext4 "
      "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0");
  ASSERT_FALSE(e.IsDestInUsrLocal());
}

TEST(MountEntryTest, Namespace) {
  MountEntry e("nsfs /run/netns/connected_netns_0 nsfs rw 0 0");
  ASSERT_TRUE(e.IsNamespaceBindMount());
}

TEST(MountEntryTest, OtherTypesAreNotNamespaceMounts) {
  MountEntry e("src /run/netns/connected_netns_0 msphs rw 0 0");
  ASSERT_FALSE(e.IsNamespaceBindMount());
}

}  // namespace secanomalyd
