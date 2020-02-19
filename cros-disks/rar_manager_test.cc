// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rar_manager.h"

#include <brillo/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {

std::ostream& operator<<(std::ostream& out, const FUSEMounter::BindPath& x) {
  return out << "{ path: " << quote(x.path) << ", writable: " << x.writable
             << ", recursive: " << x.recursive << " }";
}

bool operator==(const FUSEMounter::BindPath& a,
                const FUSEMounter::BindPath& b) {
  return a.path == b.path && a.writable == b.writable &&
         a.recursive == b.recursive;
}

namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SizeIs;

const char kMountRootDirectory[] = "/my_mount_point";

// Mock Platform implementation for testing.
class MockPlatform : public Platform {
 public:
  MOCK_METHOD(bool, PathExists, (const std::string&), (const, override));
};

}  // namespace

class RarManagerTest : public testing::Test {
 protected:
  Metrics metrics_;
  MockPlatform platform_;
  brillo::ProcessReaper reaper_;
  const RarManager manager_{kMountRootDirectory, &platform_, &metrics_,
                            &reaper_};
};

TEST_F(RarManagerTest, CanMount) {
  const MountManager& m = manager_;
  EXPECT_FALSE(m.CanMount(""));
  EXPECT_FALSE(m.CanMount(".rar"));
  EXPECT_FALSE(m.CanMount("blah.rar"));
  EXPECT_FALSE(m.CanMount("/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/x/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/Downloads/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/Downloads/x/blah.rar"));
  EXPECT_FALSE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/x/blah.rar"));
  EXPECT_FALSE(m.CanMount("/home/chronos/user/MyFiles/blah.rar"));
  EXPECT_FALSE(
      m.CanMount("/home/x/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/archive/y/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/removable/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("/media/x/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("/media/x/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/Blah.Rar"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/BLAH.RAR"));
  EXPECT_FALSE(m.CanMount("x/media/fuse/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("media/fuse/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("/media/fuse/y/blah.ram"));
  EXPECT_FALSE(m.CanMount("file:///media/fuse/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("ssh:///media/fuse/y/blah.rar"));
}

TEST_F(RarManagerTest, SuggestMountPath) {
  const RarManager& m = manager_;
  const std::string expected_mount_path =
      std::string(kMountRootDirectory) + "/doc.rar";
  EXPECT_EQ(m.SuggestMountPath("/home/chronos/user/Downloads/doc.rar"),
            expected_mount_path);
  EXPECT_EQ(m.SuggestMountPath("/media/archive/test.rar/doc.rar"),
            expected_mount_path);
}

TEST_F(RarManagerTest, Increment) {
  std::string s;
  const auto inc = [&s] { return RarManager::Increment(s.begin(), s.end()); };

  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "");

  s = "0";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "1");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "2");

  s = "8";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "9");
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "0");

  s = "00";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "01");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "02");

  s = "09";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "10");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "11");

  s = "98";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "99");
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "00");

  s = "000";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "001");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "002");

  s = "009";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "010");

  s = "099";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "100");

  s = "999";
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "000");

  s = "a";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "b");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "c");

  s = "y";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "z");
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "a");

  s = "A";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "B");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "C");

  s = "Y";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "Z");
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "A");

  s = "r00";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "r01");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "r02");

  s = "r98";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "r99");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "s00");

  s = "z98";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "z99");
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "a00");

  s = "R00";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "R01");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "R02");

  s = "R98";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "R99");
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "S00");

  s = "Z98";
  EXPECT_TRUE(inc());
  EXPECT_EQ(s, "Z99");
  EXPECT_FALSE(inc());
  EXPECT_EQ(s, "A00");
}

TEST_F(RarManagerTest, ParseDigits) {
  EXPECT_THAT(RarManager::ParseDigits(""), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("0"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits(".rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("part.rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits(".part.rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("blah.part.rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("blah0.part.rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("/blah.part.rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("0.rar"), IsEmpty());
  EXPECT_THAT(RarManager::ParseDigits("part0.rar"), IsEmpty());
  EXPECT_EQ(RarManager::ParseDigits(".part0.rar"),
            (RarManager::IndexRange{5, 6}));
  EXPECT_EQ(RarManager::ParseDigits("blah.part0.rar"),
            (RarManager::IndexRange{9, 10}));
  EXPECT_EQ(RarManager::ParseDigits("/blah.part0.rar"),
            (RarManager::IndexRange{10, 11}));
  EXPECT_EQ(RarManager::ParseDigits("/some/path/blah.part0.rar"),
            (RarManager::IndexRange{20, 21}));
  EXPECT_EQ(RarManager::ParseDigits(".part9.rar"),
            (RarManager::IndexRange{5, 6}));
  EXPECT_EQ(RarManager::ParseDigits("blah.part9.rar"),
            (RarManager::IndexRange{9, 10}));
  EXPECT_EQ(RarManager::ParseDigits("/blah.part9.rar"),
            (RarManager::IndexRange{10, 11}));
  EXPECT_EQ(RarManager::ParseDigits("/some/path/blah.part9.rar"),
            (RarManager::IndexRange{20, 21}));
  EXPECT_EQ(RarManager::ParseDigits(".part2468097531.rar"),
            (RarManager::IndexRange{5, 15}));
  EXPECT_EQ(RarManager::ParseDigits("blah.part2468097531.rar"),
            (RarManager::IndexRange{9, 19}));
  EXPECT_EQ(RarManager::ParseDigits("/blah.part2468097531.rar"),
            (RarManager::IndexRange{10, 20}));
  EXPECT_EQ(RarManager::ParseDigits("/some/path/blah.part2468097531.rar"),
            (RarManager::IndexRange{20, 30}));
  EXPECT_EQ(RarManager::ParseDigits("Blah.Part0.Rar"),
            (RarManager::IndexRange{9, 10}));
  EXPECT_EQ(RarManager::ParseDigits("BLAH.PART0.RAR"),
            (RarManager::IndexRange{9, 10}));
}

TEST_F(RarManagerTest, GetBindPathsWithOldNamingScheme) {
  const RarManager& m = manager_;
  EXPECT_THAT(m.GetBindPaths("poi"),
              ElementsAreArray<FUSEMounter::BindPath>({{"poi"}}));

  EXPECT_CALL(platform_, PathExists("poi.r00")).WillOnce(Return(false));
  EXPECT_THAT(m.GetBindPaths("poi.rar"),
              ElementsAreArray<FUSEMounter::BindPath>({{"poi.rar"}}));

  EXPECT_CALL(platform_, PathExists("poi.r00")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("poi.r01")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("poi.r02")).WillOnce(Return(false));
  EXPECT_THAT(m.GetBindPaths("poi.rar"),
              ElementsAreArray<FUSEMounter::BindPath>(
                  {{"poi.rar"}, {"poi.r00"}, {"poi.r01"}}));

  EXPECT_CALL(platform_, PathExists("POI.R00")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("POI.R01")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("POI.R02")).WillOnce(Return(false));
  EXPECT_THAT(m.GetBindPaths("POI.RAR"),
              ElementsAreArray<FUSEMounter::BindPath>(
                  {{"POI.RAR"}, {"POI.R00"}, {"POI.R01"}}));
}

TEST_F(RarManagerTest, GetBindPathsWithNewNamingScheme) {
  const RarManager& m = manager_;

  EXPECT_CALL(platform_, PathExists("poi.part1.rar")).WillOnce(Return(false));
  EXPECT_THAT(m.GetBindPaths("poi.part2.rar"),
              ElementsAreArray<FUSEMounter::BindPath>({{"poi.part2.rar"}}));

  EXPECT_CALL(platform_, PathExists("poi.part1.rar")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("poi.part2.rar")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("poi.part3.rar")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("poi.part4.rar")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("poi.part5.rar")).WillOnce(Return(false));
  EXPECT_THAT(m.GetBindPaths("poi.part2.rar"),
              ElementsAreArray<FUSEMounter::BindPath>({{"poi.part2.rar"},
                                                       {"poi.part1.rar"},
                                                       {"poi.part3.rar"},
                                                       {"poi.part4.rar"}}));

  EXPECT_CALL(platform_, PathExists("POI.PART1.RAR")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("POI.PART2.RAR")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("POI.PART3.RAR")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("POI.PART4.RAR")).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("POI.PART5.RAR")).WillOnce(Return(false));
  EXPECT_THAT(m.GetBindPaths("POI.PART2.RAR"),
              ElementsAreArray<FUSEMounter::BindPath>({{"POI.PART2.RAR"},
                                                       {"POI.PART1.RAR"},
                                                       {"POI.PART3.RAR"},
                                                       {"POI.PART4.RAR"}}));
}

TEST_F(RarManagerTest, GetBindPathsStopsOnOverflow) {
  const RarManager& m = manager_;

  EXPECT_CALL(platform_, PathExists(_)).WillRepeatedly(Return(true));

  EXPECT_THAT(m.GetBindPaths("poi.rar"), SizeIs(901));
  EXPECT_THAT(m.GetBindPaths("POI.RAR"), SizeIs(901));
  EXPECT_THAT(m.GetBindPaths("poi.part1.rar"), SizeIs(9));
  EXPECT_THAT(m.GetBindPaths("POI.PART1.RAR"), SizeIs(9));
  EXPECT_THAT(m.GetBindPaths("poi.part01.rar"), SizeIs(99));
  EXPECT_THAT(m.GetBindPaths("POI.PART01.RAR"), SizeIs(99));
  EXPECT_THAT(m.GetBindPaths("poi.part001.rar"), SizeIs(999));
  EXPECT_THAT(m.GetBindPaths("POI.PART001.RAR"), SizeIs(999));
}

}  // namespace cros_disks
