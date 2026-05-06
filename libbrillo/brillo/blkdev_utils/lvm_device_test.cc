// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "brillo/blkdev_utils/mock_lvm.h"

using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Optional;

namespace brillo {

TEST(LvmCommandRunner, RunCommandNormal) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "def", "ghi"});
  EXPECT_THAT(cmd, Optional(Eq("abc def ghi")));
}

TEST(LvmCommandRunner, RunCommandFailsOnComment) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "def", "#", "comment"});
  EXPECT_THAT(cmd, Eq(std::nullopt));
}

TEST(LvmCommandRunner, RunCommandFullyQuoted) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "'def'", "ghi"});
  EXPECT_THAT(cmd, Optional(Eq("abc 'def' ghi")));
}

TEST(LvmCommandRunner, RunCommandFullyQuotedWithSpecials) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "'d e \"f\"'", "ghi"});
  EXPECT_THAT(cmd, Optional(Eq("abc 'd e \"f\"' ghi")));
}

TEST(LvmCommandRunner, RunCommandFailsOnNestedQuote) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "'d'e'f'", "ghi"});
  EXPECT_THAT(cmd, Eq(std::nullopt));
}

TEST(LvmCommandRunner, RunCommandFullyDoubleQuoted) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "\"def\"", "ghi"});
  EXPECT_THAT(cmd, Optional(Eq("abc \"def\" ghi")));
}

TEST(LvmCommandRunner, RunCommandFullyDoubleQuotedWithSpecials) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "\"d e 'f'\"", "ghi"});
  EXPECT_THAT(cmd, Optional(Eq("abc \"d e 'f'\" ghi")));
}

TEST(LvmCommandRunner, RunCommandFailsOnDoubleQuote) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc", "\"d\"e\"f\"", "ghi"});
  EXPECT_THAT(cmd, Eq(std::nullopt));
}

TEST(LvmCommandRunner, RunCommandFailsOnSpace) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc def ghi"});
  EXPECT_THAT(cmd, Eq(std::nullopt));
}

TEST(LvmCommandRunner, RunCommandFailsOnTab) {
  auto cmd = LvmCommandRunner::JoinCommand({"abc\tdef\tghi"});
  EXPECT_THAT(cmd, Eq(std::nullopt));
}

TEST(PhysicalVolumeTest, InvalidPhysicalVolumeTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  PhysicalVolume pv(base::FilePath(""), lvm);

  EXPECT_FALSE(pv.Check());
  EXPECT_FALSE(pv.Repair());
  EXPECT_FALSE(pv.Remove());
}

TEST(PhysicalVolumeTest, PhysicalVolumeSanityTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  base::FilePath device_path("/dev/sda1");
  PhysicalVolume pv(device_path, lvm);

  EXPECT_EQ(device_path, pv.GetPath());
  EXPECT_TRUE(pv.Remove());
  EXPECT_EQ(base::FilePath(""), pv.GetPath());
}

TEST(VolumeGroupTest, InvalidVolumeGroupTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  VolumeGroup vg("", lvm);

  EXPECT_FALSE(vg.Check());
  EXPECT_FALSE(vg.Activate());
  EXPECT_FALSE(vg.Deactivate());
  EXPECT_FALSE(vg.Repair());
  EXPECT_FALSE(vg.Remove());
}

TEST(VolumeGroupTest, VolumeGroupSanityTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  VolumeGroup vg("FooBar", lvm);

  EXPECT_EQ(base::FilePath("/dev/FooBar"), vg.GetPath());
  EXPECT_EQ("FooBar", vg.GetName());

  EXPECT_TRUE(vg.Remove());
  EXPECT_EQ("", vg.GetName());
}

TEST(VolumeGroupTest, VolumeGroupRemoveRunnerTest) {
  VolumeGroup vg("foobar", nullptr);
  EXPECT_FALSE(vg.Rename("foo"));
}

TEST(VolumeGroupTest, VolumeGroupRemoveEmptySourceTest) {
  auto lvm = std::make_shared<testing::StrictMock<MockLvmCommandRunner>>();
  VolumeGroup vg("", lvm);
  EXPECT_FALSE(vg.Rename("foobar"));
}

TEST(VolumeGroupTest, VolumeGroupRemoveEmptyTargetTest) {
  auto lvm = std::make_shared<testing::StrictMock<MockLvmCommandRunner>>();
  VolumeGroup vg("foobar", lvm);
  EXPECT_FALSE(vg.Rename(""));
}

TEST(VolumeGroupTest, VolumeGroupRemoveTest) {
  auto lvm = std::make_shared<testing::StrictMock<MockLvmCommandRunner>>();
  EXPECT_CALL(*lvm, RunCommand(_)).WillOnce(testing::Return(true));
  VolumeGroup vg("foobar", lvm);
  EXPECT_TRUE(vg.Rename("foo"));
}

TEST(ThinpoolTest, InvalidThinpoolTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  Thinpool thinpool("", "", lvm);

  EXPECT_FALSE(thinpool.Check());
  EXPECT_FALSE(thinpool.Activate());
  EXPECT_FALSE(thinpool.Deactivate());
  EXPECT_FALSE(thinpool.Repair());
  EXPECT_FALSE(thinpool.Remove());
}

TEST(ThinpoolTest, ThinpoolSanityTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  Thinpool thinpool("Foo", "Bar", lvm);

  EXPECT_EQ("Bar/Foo", thinpool.GetName());
  EXPECT_EQ("Foo", thinpool.GetRawName());
  EXPECT_EQ("Bar", thinpool.GetVolumeGroupName());
  EXPECT_TRUE(thinpool.Remove());
  EXPECT_EQ("", thinpool.GetName());
}

TEST(ThinpoolTest, ThinpoolSpaceTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  Thinpool thinpool("foo", "bar", lvm);
  constexpr const char kStatus[] =
      "3 5048/226304 155436/1779200 - rw discard_passdown "
      "queue_if_no_space - 1024";
  auto fn = FakeRunDmStatusIoctl(0, 227737600, kStatus);

  EXPECT_CALL(*lvm, RunDmIoctl(_, _)).WillRepeatedly(testing::Invoke(fn));

  int64_t total_space, free_space;
  EXPECT_TRUE(thinpool.GetTotalSpace(&total_space));
  EXPECT_TRUE(thinpool.GetFreeSpace(&free_space));
  EXPECT_EQ(total_space, 116601651200LL);
  EXPECT_EQ(free_space, 106410666885LL);
}

TEST(LogicalVolumeTest, InvalidLogicalVolumeTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  LogicalVolume lv("", "", lvm);

  EXPECT_FALSE(lv.Activate());
  EXPECT_FALSE(lv.Deactivate());
  EXPECT_FALSE(lv.Remove());
}

TEST(LogicalVolumeTest, LogicalVolumeSanityTest) {
  auto lvm = std::make_shared<MockLvmCommandRunner>();
  LogicalVolume lv("Foo", "Bar", lvm);

  EXPECT_EQ(base::FilePath("/dev/Bar/Foo"), lv.GetPath());
  EXPECT_EQ("Bar/Foo", lv.GetName());
  EXPECT_EQ("Foo", lv.GetRawName());
  EXPECT_EQ("Bar", lv.GetVolumeGroupName());
  EXPECT_TRUE(lv.Rename("Baz"));
  EXPECT_EQ("Bar/Baz", lv.GetName());
  EXPECT_EQ("Baz", lv.GetRawName());
  EXPECT_EQ("Bar", lv.GetVolumeGroupName());
  EXPECT_TRUE(lv.Remove());
  EXPECT_EQ("", lv.GetName());
}

}  // namespace brillo
