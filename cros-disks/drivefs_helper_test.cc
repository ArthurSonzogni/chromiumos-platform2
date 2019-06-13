// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/drivefs_helper.h"

#include <sys/mount.h>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

using testing::_;
using testing::DoAll;
using testing::EndsWith;
using testing::Eq;
using testing::HasSubstr;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;

const uid_t kOldUID = 200;
const uid_t kOldGID = 201;
const uid_t kFilesUID = 700;
const uid_t kFilesGID = 701;
const uid_t kFilesAccessGID = 1501;
const uid_t kOtherUID = 400;

constexpr char kMyFiles[] = "/home/chronos/user/MyFiles";

// Mock Platform implementation for testing.
class MockPlatform : public Platform {
 public:
  MockPlatform() {
    ON_CALL(*this, GetUserAndGroupId(_, _, _))
        .WillByDefault(Invoke(this, &MockPlatform::GetUserAndGroupIdImpl));
    ON_CALL(*this, GetGroupId(_, _))
        .WillByDefault(Invoke(this, &MockPlatform::GetGroupIdImpl));
    ON_CALL(*this, GetRealPath(_, _))
        .WillByDefault(Invoke(this, &MockPlatform::GetRealPathImpl));
    ON_CALL(*this, IsDirectoryEmpty(_)).WillByDefault(Return(true));
    ON_CALL(*this, DirectoryExists(_)).WillByDefault(Return(true));
    ON_CALL(*this, PathExists(EndsWith("-seccomp.policy")))
        .WillByDefault(Return(false));
  }

  bool SetUp() { return temp_dir_.CreateUniqueTempDir(); }

  const base::FilePath& datadir() const { return temp_dir_.GetPath(); }

  MOCK_CONST_METHOD2(GetRealPath, bool(const std::string&, std::string*));
  MOCK_CONST_METHOD3(GetUserAndGroupId,
                     bool(const std::string&, uid_t* user_id, gid_t* group_id));
  MOCK_CONST_METHOD2(GetGroupId, bool(const std::string&, gid_t* group_id));
  MOCK_CONST_METHOD1(PathExists, bool(const std::string& path));
  MOCK_CONST_METHOD1(DirectoryExists, bool(const std::string& path));
  MOCK_CONST_METHOD1(IsDirectoryEmpty, bool(const std::string& path));
  MOCK_CONST_METHOD1(CreateDirectory, bool(const std::string& path));
  MOCK_CONST_METHOD1(RemoveEmptyDirectory, bool(const std::string& path));
  MOCK_CONST_METHOD3(SetOwnership,
                     bool(const std::string& path,
                          uid_t user_id,
                          gid_t group_id));
  MOCK_CONST_METHOD3(GetOwnership,
                     bool(const std::string& path,
                          uid_t* user_id,
                          gid_t* group_id));
  MOCK_CONST_METHOD2(SetPermissions,
                     bool(const std::string& path, mode_t mode));

 private:
  bool GetRealPathImpl(const std::string& path, std::string* real_path) const {
    if (path.find("baz") != std::string::npos) {
      *real_path = "/baz/qux";
      return true;
    }
    *real_path = datadir().value();
    return true;
  }

  bool GetUserAndGroupIdImpl(const std::string& user,
                             uid_t* user_id,
                             gid_t* group_id) const {
    if (user == FUSEHelper::kFilesUser) {
      if (user_id)
        *user_id = kFilesUID;
      if (group_id)
        *group_id = kFilesGID;
      return true;
    }
    if (user == "fuse-drivefs") {
      if (user_id)
        *user_id = kOldUID;
      if (group_id)
        *group_id = kOldGID;
      return true;
    }
    return false;
  }

  bool GetGroupIdImpl(const std::string& group, gid_t* group_id) const {
    if (group == FUSEHelper::kFilesGroup) {
      if (group_id)
        *group_id = kFilesAccessGID;
      return true;
    }
    return false;
  }

  base::ScopedTempDir temp_dir_;
};

class TestDrivefsHelper : public DrivefsHelper {
 public:
  explicit TestDrivefsHelper(const Platform* platform)
      : DrivefsHelper(platform) {
    ON_CALL(*this, SetupDirectoryForFUSEAccess(_))
        .WillByDefault(Invoke(
            this,
            &TestDrivefsHelper::ForwardSetupDirectoryForFUSEAccessToImpl));
    ON_CALL(*this, CheckMyFilesPermissions(_))
        .WillByDefault(Invoke(
            this, &TestDrivefsHelper::ForwardCheckMyFilesPermissionsToImpl));
  }

  MOCK_CONST_METHOD1(SetupDirectoryForFUSEAccess,
                     bool(const base::FilePath& dirpath));
  MOCK_CONST_METHOD1(CheckMyFilesPermissions,
                     bool(const base::FilePath& dirpath));

 private:
  bool ForwardSetupDirectoryForFUSEAccessToImpl(const base::FilePath& path) {
    return DrivefsHelper::SetupDirectoryForFUSEAccess(path);
  }

  bool ForwardCheckMyFilesPermissionsToImpl(const base::FilePath& path) {
    return DrivefsHelper::CheckMyFilesPermissions(path);
  }
};

class DrivefsHelperTest : public ::testing::Test {
 public:
  DrivefsHelperTest() : helper_(&platform_) {}

  void SetUp() override { ASSERT_TRUE(platform_.SetUp()); }

 protected:
  bool SetupDirectoryForFUSEAccess(const std::string& dir) {
    return helper_.SetupDirectoryForFUSEAccess(base::FilePath(dir));
  }

  bool CheckMyFilesPermissions(const std::string& dir) {
    return helper_.CheckMyFilesPermissions(base::FilePath(dir));
  }

  MockPlatform platform_;
  TestDrivefsHelper helper_;
};

TEST_F(DrivefsHelperTest, CreateMounter) {
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(platform_.datadir()))
      .WillOnce(Return(true));

  auto mounter = helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"),
      {"rw", "datadir=/foo//bar/./", "datadir=/ignored/second/datadir/value"});
  ASSERT_TRUE(mounter);

  EXPECT_EQ("drivefs", mounter->filesystem_type());
  EXPECT_TRUE(mounter->source().empty());
  EXPECT_EQ("/media/fuse/drivefs/id", mounter->target_path().value());
  auto options_string = mounter->mount_options().ToString();
  EXPECT_THAT(options_string,
              HasSubstr("datadir=" + platform_.datadir().value()));
  EXPECT_THAT(options_string, HasSubstr("identity=id"));
  EXPECT_THAT(options_string, HasSubstr("rw"));
  EXPECT_THAT(options_string, HasSubstr("uid=700"));
  EXPECT_THAT(options_string, HasSubstr("gid=1501"));
}

TEST_F(DrivefsHelperTest, CreateMounterWithMyFiles) {
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(platform_.datadir()))
      .WillOnce(Return(true));
  EXPECT_CALL(helper_, CheckMyFilesPermissions(base::FilePath("/baz/qux")))
      .WillOnce(Return(true));

  auto mounter = helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"),
      {"rw", "datadir=/foo//bar/./", "datadir=/ignored/second/datadir/value",
       "myfiles=/baz/.//qux/", "myfiles=/ignored/second/"});
  ASSERT_TRUE(mounter);

  EXPECT_EQ("drivefs", mounter->filesystem_type());
  EXPECT_TRUE(mounter->source().empty());
  EXPECT_EQ("/media/fuse/drivefs/id", mounter->target_path().value());
  auto options_string = mounter->mount_options().ToString();
  EXPECT_THAT(options_string,
              HasSubstr("datadir=" + platform_.datadir().value()));
  EXPECT_THAT(options_string, HasSubstr("myfiles=/baz/qux"));
  EXPECT_THAT(options_string, HasSubstr("identity=id"));
  EXPECT_THAT(options_string, HasSubstr("rw"));
  EXPECT_THAT(options_string, HasSubstr("uid=700"));
  EXPECT_THAT(options_string, HasSubstr("gid=1501"));
}

TEST_F(DrivefsHelperTest, CreateMounter_CreateDataDir) {
  EXPECT_CALL(platform_, DirectoryExists("/foo//bar/")).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetRealPath("/foo", _))
      .WillOnce(
          DoAll(SetArgPointee<1>(platform_.datadir().value()), Return(true)));
  EXPECT_CALL(helper_,
              SetupDirectoryForFUSEAccess(platform_.datadir().Append("bar")))
      .WillOnce(Return(true));

  auto mounter = helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"),
      {"rw", "datadir=/foo//bar/", "datadir=/ignored/second/datadir/value"});
  ASSERT_TRUE(mounter);

  EXPECT_EQ("drivefs", mounter->filesystem_type());
  EXPECT_TRUE(mounter->source().empty());
  EXPECT_EQ("/media/fuse/drivefs/id", mounter->target_path().value());
  auto options_string = mounter->mount_options().ToString();
  EXPECT_THAT(options_string,
              HasSubstr("datadir=" + platform_.datadir().value()));
  EXPECT_THAT(options_string, HasSubstr("identity=id"));
  EXPECT_THAT(options_string, HasSubstr("rw"));
  EXPECT_THAT(options_string, HasSubstr("uid=700"));
  EXPECT_THAT(options_string, HasSubstr("gid=1501"));
}

TEST_F(DrivefsHelperTest, CreateMounter_GetUserAndGroupIdFails) {
  EXPECT_CALL(platform_, GetUserAndGroupId(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(platform_.datadir()))
      .Times(0);

  EXPECT_FALSE(helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"), {"rw", "datadir=/foo/bar"}));
}

TEST_F(DrivefsHelperTest, CreateMounter_GetAndGroupIdFails) {
  EXPECT_CALL(platform_, GetGroupId(_, _)).WillOnce(Return(false));
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(platform_.datadir()))
      .Times(0);

  EXPECT_FALSE(helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"), {"rw", "datadir=/foo/bar"}));
}

TEST_F(DrivefsHelperTest, CreateMounter_GetRealPathFails_DirectoryExists) {
  EXPECT_CALL(platform_, GetRealPath("/foo/bar", _)).WillOnce(Return(false));
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(_)).Times(0);

  EXPECT_FALSE(helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"), {"rw", "datadir=/foo/bar"}));
}

TEST_F(DrivefsHelperTest, CreateMounter_GetRealPathFails_DirectoryDoesntExist) {
  EXPECT_CALL(platform_, DirectoryExists("/foo/bar")).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetRealPath("/foo", _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetGroupId(_, _)).Times(0);
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(_)).Times(0);

  EXPECT_FALSE(helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"), {"rw", "datadir=/foo/bar"}));
}

TEST_F(DrivefsHelperTest, CreateMounter_InvalidPath) {
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(_)).Times(0);

  for (const auto* path : {"relative/path", "/foo/../bar", ".", ".."}) {
    EXPECT_FALSE(helper_.CreateMounter(base::FilePath("/tmp/working_dir"),
                                       Uri::Parse("drivefs://id"),
                                       base::FilePath("/media/fuse/drivefs/id"),
                                       {"rw", "datadir=" + std::string(path)}));
  }
}

TEST_F(DrivefsHelperTest, CreateMounter_NoDatadir) {
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(_)).Times(0);

  EXPECT_FALSE(helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"), {"rw"}));
}

TEST_F(DrivefsHelperTest, CreateMounter_SetupDirectoryFails) {
  EXPECT_CALL(helper_, SetupDirectoryForFUSEAccess(platform_.datadir()))
      .WillOnce(Return(false));

  EXPECT_FALSE(helper_.CreateMounter(
      base::FilePath("/tmp/working_dir"), Uri::Parse("drivefs://id"),
      base::FilePath("/media/fuse/drivefs/id"), {"rw", "datadir=/foo/bar"}));
}

// Verifies that SetupDirectoryForFUSEAccess crashes if path is unsafe.
TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_UnsafePath) {
  EXPECT_DEATH(SetupDirectoryForFUSEAccess("foo"), ".*");
  EXPECT_DEATH(SetupDirectoryForFUSEAccess("../foo"), ".*");
  EXPECT_DEATH(SetupDirectoryForFUSEAccess("/bar/../foo"), ".*");
  EXPECT_DEATH(SetupDirectoryForFUSEAccess("/../foo"), ".*");
}

// Verifies that SetupDirectoryForFUSEAccess creates directory with
// correct access if there was no directory initially.
TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_NoDir) {
  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(platform_.datadir().value(), 0770))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().value(), kFilesUID,
                                      kFilesAccessGID))
      .WillOnce(Return(true));
  EXPECT_TRUE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

// Verifies that SetupDirectoryForFUSEAccess fails if there was no
// directory initially and can't create one.
TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_NoDir_CantCreate) {
  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(platform_.datadir().value()))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, SetPermissions(_, _)).Times(0);
  EXPECT_CALL(platform_, SetOwnership(_, _, _)).Times(0);
  EXPECT_FALSE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

// Verifies that SetupDirectoryForFUSEAccess fails if chmod fails.
TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_NoDir_CantChmod) {
  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(platform_.datadir().value(), 0770));
  EXPECT_FALSE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

// Verifies that SetupDirectoryForFUSEAccess fails if can't get attributes
// of an existing directory.
TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_CantStat) {
  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(platform_.datadir().value(), _, _));
  EXPECT_CALL(platform_, SetOwnership(_, _, _)).Times(0);
  EXPECT_FALSE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

// Verifies that SetupDirectoryForFUSEAccess succeeds with shortcut
// if directory already has correct owner.
TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_Owned) {
  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(platform_.datadir().value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFilesUID),
                      SetArgPointee<2>(kFilesAccessGID), Return(true)));
  EXPECT_CALL(platform_, SetOwnership(_, _, _)).Times(0);
  EXPECT_TRUE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

// Verifies that SetupDirectoryForFUSEAccess updates ownership to match the
// expected owner if the old owner was as expected.
TEST_F(DrivefsHelperTest,
       SetupDirectoryForFUSEAccess_AlreadyExistsWithOldOwner) {
  base::CreateDirectory(platform_.datadir().Append("foo"));
  base::WriteFile(platform_.datadir().Append("foo").Append("qux"), "a", 1);
  base::CreateDirectory(platform_.datadir().Append("bar"));
  base::WriteFile(platform_.datadir().Append("bar").Append("baz"), "a", 1);

  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(platform_.datadir().value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kOldUID),
                      SetArgPointee<2>(kFilesAccessGID), Return(true)));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().value(), kOldUID,
                                      kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_,
              GetOwnership(platform_.datadir().Append("bar").value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFilesUID),
                      SetArgPointee<2>(kFilesAccessGID), Return(true)));

  EXPECT_CALL(platform_,
              GetOwnership(platform_.datadir().Append("foo").value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kOldUID), SetArgPointee<2>(kOldGID),
                      Return(true)));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().Append("foo").value(),
                                      kOldUID, kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(
      platform_,
      SetOwnership(platform_.datadir().Append("foo").Append("qux").value(),
                   kFilesUID, kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().Append("foo").value(),
                                      kFilesUID, kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().value(), kFilesUID,
                                      kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_TRUE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

TEST_F(DrivefsHelperTest, SetupDirectoryForFUSEAccess_AlreadyExists_CantChown) {
  base::CreateDirectory(platform_.datadir().Append("foo"));
  base::WriteFile(platform_.datadir().Append("foo").Append("qux"), "a", 1);
  base::CreateDirectory(platform_.datadir().Append("bar"));
  base::WriteFile(platform_.datadir().Append("bar").Append("baz"), "a", 1);

  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(platform_.datadir().value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kOldUID),
                      SetArgPointee<2>(kFilesAccessGID), Return(true)));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().value(), kOldUID,
                                      kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_,
              GetOwnership(platform_.datadir().Append("bar").value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFilesUID),
                      SetArgPointee<2>(kFilesAccessGID), Return(true)));

  EXPECT_CALL(platform_,
              GetOwnership(platform_.datadir().Append("foo").value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kOldUID), SetArgPointee<2>(kOldGID),
                      Return(true)));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().Append("foo").value(),
                                      kOldUID, kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(
      platform_,
      SetOwnership(platform_.datadir().Append("foo").Append("qux").value(),
                   kFilesUID, kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().Append("foo").value(),
                                      kFilesUID, kFilesAccessGID))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, SetOwnership(platform_.datadir().value(), kFilesUID,
                                      kFilesAccessGID))
      .WillOnce(Return(false));

  EXPECT_FALSE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

// Verifies that SetupDirectoryForFUSEAccess refuses to update ownership from an
// unexpected old uid.
TEST_F(DrivefsHelperTest,
       SetupDirectoryForFUSEAccess_AlreadyExistsWithUnexpectedOwner) {
  base::CreateDirectory(platform_.datadir().Append("foo"));
  base::WriteFile(platform_.datadir().Append("foo").Append("qux"), "a", 1);
  base::CreateDirectory(platform_.datadir().Append("bar"));
  base::WriteFile(platform_.datadir().Append("bar").Append("baz"), "a", 1);

  EXPECT_CALL(platform_, DirectoryExists(platform_.datadir().value()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(platform_.datadir().value(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kOtherUID), SetArgPointee<2>(kOldGID),
                      Return(true)));

  EXPECT_FALSE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

TEST_F(DrivefsHelperTest,
       SetupDirectoryForFUSEAccess_AlreadyExistsOldOwnerTooDeep) {
  ON_CALL(platform_, GetOwnership(_, _, _))
      .WillByDefault((DoAll(SetArgPointee<1>(kOldUID),
                            SetArgPointee<2>(kOldGID), Return(true))));
  ON_CALL(platform_, SetOwnership(_, _, _)).WillByDefault((Return(true)));

  ASSERT_TRUE(base::CreateDirectory(platform_.datadir().Append("1/2/3/4/5/6")));

  EXPECT_FALSE(SetupDirectoryForFUSEAccess(platform_.datadir().value()));
}

TEST_F(DrivefsHelperTest, CheckMyFilesPermissions_Success) {
  EXPECT_CALL(platform_, DirectoryExists(kMyFiles)).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(kMyFiles, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFilesUID), Return(true)));

  EXPECT_TRUE(CheckMyFilesPermissions(kMyFiles));
}

TEST_F(DrivefsHelperTest, CheckMyFilesPermissions_WrongOwner) {
  EXPECT_CALL(platform_, DirectoryExists(kMyFiles)).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetOwnership(kMyFiles, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kOtherUID), Return(true)));

  EXPECT_FALSE(CheckMyFilesPermissions(kMyFiles));
}

TEST_F(DrivefsHelperTest, CheckMyFilesPermissions_InvalidUser) {
  EXPECT_CALL(platform_, GetUserAndGroupId(FUSEHelper::kFilesUser, _, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(CheckMyFilesPermissions(kMyFiles));
}

}  // namespace
}  // namespace cros_disks
