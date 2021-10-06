// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for functionality in mounts.h.

#include "secanomalyd/mounts.h"

#include <string>

#include <gtest/gtest.h>

#include <base/optional.h>

TEST(MountsTest, EmptyString) {
  MaybeMountEntries entries = ReadMountsFromString("");
  ASSERT_EQ(entries, base::nullopt);
}

TEST(MountsTest, ActualMounts) {
  const std::string mounts =
      "/dev/sda1 /mnt/stateful_partition ext4 "
      "rw,seclabel,nosuid,nodev,noexec,noatime,"
      "resgid=20119,commit=600,data=ordered 0 0\n"
      //
      "/dev/sda1 /usr/local ext4 "
      "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0\n"
      //
      "/dev/sdb1 /media/removable/USB\040Drive ext2 "
      "rw,dirsync,nosuid,nodev,noexec,seclabel,relatime,nosymfollow\n"
      //
      "fuse:/home/chronos/u-f0df208cd7759644d43f8d7c4c5900e4a4875275/MyFiles/"
      "Downloads/sample.rar /media/archive/sample.rar fuse.rarfs "
      "ro,dirsync,nosuid,nodev,noexec,relatime,nosymfollow,"
      "user_id=1000,group_id=1001,default_permissions,allow_other 0 0";

  MaybeMountEntries maybe_entries = ReadMountsFromString(mounts);
  ASSERT_TRUE(maybe_entries.has_value());
  MountEntries entries = maybe_entries.value();
  ASSERT_EQ(entries.size(), 4u);

  ASSERT_EQ(entries[0].dest(), base::FilePath("/mnt/stateful_partition"));
  ASSERT_EQ(entries[1].src(), base::FilePath("/dev/sda1"));
  ASSERT_EQ(entries[3].type(), "fuse.rarfs");
}
