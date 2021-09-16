// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/dircrypto_data_migrator/migration_helper.h"

#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>

#include "cryptohome/migration_type.h"
#include "cryptohome/mock_platform.h"

extern "C" {
#include <linux/fs.h>
}

using base::FilePath;
using base::ScopedTempDir;
using testing::_;
using testing::DoDefault;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::SetErrnoAndReturn;
using testing::Values;

namespace cryptohome {
namespace dircrypto_data_migrator {

namespace {
constexpr uint64_t kDefaultChunkSize = 128;
constexpr char kMtimeXattrName[] = "user.mtime";
constexpr char kAtimeXattrName[] = "user.atime";

constexpr char kStatusFilesDir[] = "/home/.shadow/deadbeef/status_dir";
constexpr char kFromDir[] = "/home/.shadow/deadbeef/temporary_mount";
constexpr char kToDir[] = "/home/.shadow/deadbeef/mount";
}  // namespace

class MigrationHelperTest : public ::testing::Test {
 public:
  MigrationHelperTest()
      : status_files_dir_(kStatusFilesDir),
        from_dir_(kFromDir),
        to_dir_(kToDir) {}
  virtual ~MigrationHelperTest() {}

  void SetUp() override {
    ASSERT_TRUE(platform_.CreateDirectory(status_files_dir_));
    ASSERT_TRUE(platform_.CreateDirectory(from_dir_));
    ASSERT_TRUE(platform_.CreateDirectory(to_dir_));
  }

  void ProgressCaptor(
      const user_data_auth::DircryptoMigrationProgress& progress) {
    migrated_values_.push_back(progress.current_bytes());
    total_values_.push_back(progress.total_bytes());
    status_values_.push_back(progress.status());
  }

 protected:
  NiceMock<MockPlatform> platform_;

  base::FilePath status_files_dir_;
  base::FilePath from_dir_;
  base::FilePath to_dir_;

  std::vector<uint64_t> migrated_values_;
  std::vector<uint64_t> total_values_;
  std::vector<user_data_auth::DircryptoMigrationStatus> status_values_;
};

TEST_F(MigrationHelperTest, EmptyTest) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  ASSERT_TRUE(platform_.IsDirectoryEmpty(from_dir_));
  ASSERT_TRUE(platform_.IsDirectoryEmpty(to_dir_));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
}

TEST_F(MigrationHelperTest, CopyAttributesDirectory) {
  // This test only covers permissions and xattrs.  Ownership copying requires
  // more extensive mocking and is covered in CopyOwnership test.
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kDirectory[] = "directory";
  const FilePath kFromDirPath = from_dir_.Append(kDirectory);
  ASSERT_TRUE(platform_.CreateDirectory(kFromDirPath));

  // Set some attributes to this directory.

  mode_t mode = S_ISVTX | S_IRUSR | S_IWUSR | S_IXUSR;
  ASSERT_TRUE(platform_.SetPermissions(kFromDirPath, mode));
  // GetPermissions call is needed because some bits to mode are applied
  // automatically, so our original |mode| value is not what the resulting file
  // actually has.
  ASSERT_TRUE(platform_.GetPermissions(kFromDirPath, &mode));

  constexpr char kAttrName[] = "user.attr";
  constexpr char kValue[] = "value";
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(kFromDirPath, kAttrName,
                                                 kValue, sizeof(kValue)));

  // Set ext2 attributes
  int ext2_attrs = FS_SYNC_FL | FS_NODUMP_FL;
  ASSERT_TRUE(platform_.SetExtFileAttributes(kFromDirPath, ext2_attrs));

  base::stat_wrapper_t from_stat;
  ASSERT_TRUE(platform_.Stat(kFromDirPath, &from_stat));
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  const FilePath kToDirPath = to_dir_.Append(kDirectory);
  base::stat_wrapper_t to_stat;
  ASSERT_TRUE(platform_.Stat(kToDirPath, &to_stat));
  EXPECT_TRUE(platform_.DirectoryExists(kToDirPath));

  // Verify mtime was coppied.  atime for directories is not
  // well-preserved because we have to traverse the directories to determine
  // migration size.
  EXPECT_EQ(from_stat.st_mtim.tv_sec, to_stat.st_mtim.tv_sec);
  EXPECT_EQ(from_stat.st_mtim.tv_nsec, to_stat.st_mtim.tv_nsec);

  // Verify Permissions and xattrs were copied
  mode_t to_mode;
  ASSERT_TRUE(platform_.GetPermissions(kToDirPath, &to_mode));
  EXPECT_EQ(mode, to_mode);
  char value[sizeof(kValue) + 1];
  ASSERT_TRUE(platform_.GetExtendedFileAttribute(kToDirPath, kAttrName, value,
                                                 sizeof(kValue)));
  value[sizeof(kValue)] = '\0';
  EXPECT_STREQ(kValue, value);

  // Verify ext2 flags were copied
  int new_ext2_attrs;
  ASSERT_TRUE(platform_.GetExtFileAttributes(kToDirPath, &new_ext2_attrs));
  EXPECT_EQ(ext2_attrs, new_ext2_attrs & ext2_attrs);
}

TEST_F(MigrationHelperTest, DirectoryPartiallyMigrated) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kDirectory[] = "directory";
  const FilePath kFromDirPath = from_dir_.Append(kDirectory);
  ASSERT_TRUE(platform_.CreateDirectory(kFromDirPath));
  constexpr struct timespec kMtime = {123, 456};
  constexpr struct timespec kAtime = {234, 567};
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(
      to_dir_, kMtimeXattrName, reinterpret_cast<const char*>(&kMtime),
      sizeof(kMtime)));
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(
      to_dir_, kAtimeXattrName, reinterpret_cast<const char*>(&kAtime),
      sizeof(kAtime)));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
  base::stat_wrapper_t to_stat;

  // Verify that stored timestamps for in-progress migrations are respected
  ASSERT_TRUE(platform_.Stat(to_dir_, &to_stat));
  EXPECT_EQ(kMtime.tv_sec, to_stat.st_mtim.tv_sec);
  EXPECT_EQ(kMtime.tv_nsec, to_stat.st_mtim.tv_nsec);
  EXPECT_EQ(kAtime.tv_sec, to_stat.st_atim.tv_sec);
  EXPECT_EQ(kAtime.tv_nsec, to_stat.st_atim.tv_nsec);

  // Verify subdirectory was migrated
  const FilePath kToDirPath = to_dir_.Append(kDirectory);
  EXPECT_TRUE(platform_.DirectoryExists(kToDirPath));
}

TEST_F(MigrationHelperTest, CopySymlink) {
  // This test does not cover setting ownership values as that requires more
  // extensive mocking.  Ownership copying instead is covered by the
  // CopyOwnership test.
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);
  FilePath target;

  constexpr char kFileName[] = "file";
  constexpr char kAbsLinkTarget[] = "/dev/null";
  const FilePath kTargetInMigrationDirAbsLinkTarget =
      from_dir_.Append(kFileName);
  const FilePath kRelLinkTarget = base::FilePath(kFileName);
  constexpr char kRelLinkName[] = "link1";
  constexpr char kAbsLinkName[] = "link2";
  constexpr char kTargetInMigrationDirAbsLinkName[] = "link3";
  const FilePath kFromRelLinkPath = from_dir_.Append(kRelLinkName);
  const FilePath kFromAbsLinkPath = from_dir_.Append(kAbsLinkName);
  const FilePath kFromTargetInMigrationDirAbsLinkPath =
      from_dir_.Append(kTargetInMigrationDirAbsLinkName);
  ASSERT_TRUE(platform_.CreateSymbolicLink(kFromRelLinkPath, kRelLinkTarget));
  ASSERT_TRUE(platform_.CreateSymbolicLink(kFromAbsLinkPath,
                                           base::FilePath(kAbsLinkTarget)));
  ASSERT_TRUE(platform_.CreateSymbolicLink(kFromTargetInMigrationDirAbsLinkPath,
                                           kTargetInMigrationDirAbsLinkTarget));
  base::stat_wrapper_t from_stat;
  ASSERT_TRUE(platform_.Stat(kFromRelLinkPath, &from_stat));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  const FilePath kToFilePath = to_dir_.Append(kFileName);
  const FilePath kToRelLinkPath = to_dir_.Append(kRelLinkName);
  const FilePath kToAbsLinkPath = to_dir_.Append(kAbsLinkName);
  const FilePath kToTargetInMigrationDirAbsLinkPath =
      to_dir_.Append(kTargetInMigrationDirAbsLinkName);
  const FilePath kExpectedTargetInMigrationDirAbsLinkTarget =
      to_dir_.Append(kFileName);

  // Verify that timestamps were updated appropriately.
  base::stat_wrapper_t to_stat;
  ASSERT_TRUE(platform_.Stat(kToRelLinkPath, &to_stat));
  EXPECT_EQ(from_stat.st_atim.tv_sec, to_stat.st_atim.tv_sec);
  EXPECT_EQ(from_stat.st_atim.tv_nsec, to_stat.st_atim.tv_nsec);
  EXPECT_EQ(from_stat.st_mtim.tv_sec, to_stat.st_mtim.tv_sec);
  EXPECT_EQ(from_stat.st_mtim.tv_nsec, to_stat.st_mtim.tv_nsec);

  // Verify that all links have been copied correctly
  EXPECT_TRUE(platform_.ReadLink(kToRelLinkPath, &target));
  EXPECT_EQ(kRelLinkTarget.value(), target.value());
  EXPECT_TRUE(platform_.ReadLink(kToAbsLinkPath, &target));
  EXPECT_EQ(kAbsLinkTarget, target.value());
  EXPECT_TRUE(platform_.ReadLink(kToTargetInMigrationDirAbsLinkPath, &target));
  EXPECT_EQ(kExpectedTargetInMigrationDirAbsLinkTarget.value(), target.value());
}

TEST_F(MigrationHelperTest, OneEmptyFile) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFileName[] = "empty_file";

  ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(kFileName)));
  ASSERT_TRUE(platform_.IsDirectoryEmpty(to_dir_));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // The file is moved.
  EXPECT_FALSE(platform_.FileExists(from_dir_.Append(kFileName)));
  EXPECT_TRUE(platform_.FileExists(to_dir_.Append(kFileName)));
}

TEST_F(MigrationHelperTest, OneEmptyFileInDirectory) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kDir1[] = "directory1";
  constexpr char kDir2[] = "directory2";
  constexpr char kFileName[] = "empty_file";

  // Create directory1/directory2/empty_file in from_dir_.
  ASSERT_TRUE(platform_.CreateDirectory(from_dir_.Append(kDir1).Append(kDir2)));
  ASSERT_TRUE(platform_.TouchFileDurable(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
  ASSERT_TRUE(platform_.IsDirectoryEmpty(to_dir_));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // The file is moved.
  EXPECT_FALSE(platform_.FileExists(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
  EXPECT_TRUE(platform_.IsDirectoryEmpty(from_dir_.Append(kDir1)));
  EXPECT_TRUE(platform_.FileExists(
      to_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
}

TEST_F(MigrationHelperTest, UnreadableFile) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kDir1[] = "directory1";
  constexpr char kDir2[] = "directory2";
  constexpr char kFileName[] = "empty_file";

  // Create directory1/directory2/empty_file in from_dir_.  File will be
  // unreadable to test failure case.
  ASSERT_TRUE(platform_.CreateDirectory(from_dir_.Append(kDir1).Append(kDir2)));
  ASSERT_TRUE(platform_.TouchFileDurable(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
  ASSERT_TRUE(platform_.IsDirectoryEmpty(to_dir_));
  ASSERT_TRUE(platform_.SetPermissions(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName), S_IWUSR));

  EXPECT_FALSE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // The file is not moved.
  EXPECT_TRUE(platform_.FileExists(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
}

TEST_F(MigrationHelperTest, CopyAttributesFile) {
  // This test does not cover setting ownership values as that requires more
  // extensive mocking.  Ownership copying instead is covered by the
  // CopyOwnership test.
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFileName[] = "file";
  const FilePath kFromFilePath = from_dir_.Append(kFileName);
  const FilePath kToFilePath = to_dir_.Append(kFileName);

  ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(kFileName)));
  // Set some attributes to this file.

  mode_t mode = S_ISVTX | S_IRUSR | S_IWUSR | S_IXUSR;
  ASSERT_TRUE(platform_.SetPermissions(kFromFilePath, mode));
  // GetPermissions call is needed because some bits to mode are applied
  // automatically, so our original |mode| value is not what the resulting file
  // actually has.
  ASSERT_TRUE(platform_.GetPermissions(kFromFilePath, &mode));

  constexpr char kAttrName[] = "user.attr";
  constexpr char kValue[] = "value";
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(kFromFilePath, kAttrName,
                                                 kValue, sizeof(kValue)));
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(
      kFromFilePath, kSourceURLXattrName, kValue, sizeof(kValue)));
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(
      kFromFilePath, kReferrerURLXattrName, kValue, sizeof(kValue)));

  // Set ext2 attributes
  int ext2_attrs = FS_SYNC_FL | FS_NODUMP_FL;
  ASSERT_TRUE(platform_.SetExtFileAttributes(kFromFilePath, ext2_attrs));

  base::stat_wrapper_t from_stat;
  ASSERT_TRUE(platform_.Stat(kFromFilePath, &from_stat));
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  base::stat_wrapper_t to_stat;
  ASSERT_TRUE(platform_.Stat(kToFilePath, &to_stat));
  EXPECT_EQ(from_stat.st_atim.tv_sec, to_stat.st_atim.tv_sec);
  EXPECT_EQ(from_stat.st_atim.tv_nsec, to_stat.st_atim.tv_nsec);
  EXPECT_EQ(from_stat.st_mtim.tv_sec, to_stat.st_mtim.tv_sec);
  EXPECT_EQ(from_stat.st_mtim.tv_nsec, to_stat.st_mtim.tv_nsec);

  EXPECT_TRUE(platform_.FileExists(kToFilePath));

  mode_t permission;
  ASSERT_TRUE(platform_.GetPermissions(kToFilePath, &permission));
  EXPECT_EQ(mode, permission);

  char value[sizeof(kValue) + 1];
  ASSERT_TRUE(platform_.GetExtendedFileAttribute(kToFilePath, kAttrName, value,
                                                 sizeof(kValue)));
  value[sizeof(kValue)] = '\0';
  EXPECT_STREQ(kValue, value);

  // The temporary xatttrs for storing mtime/atime should be removed.
  ASSERT_FALSE(platform_.GetExtendedFileAttribute(kToFilePath, kMtimeXattrName,
                                                  nullptr, 0));
  ASSERT_EQ(ENODATA, errno);
  ASSERT_FALSE(platform_.GetExtendedFileAttribute(kToFilePath, kAtimeXattrName,
                                                  nullptr, 0));
  ASSERT_EQ(ENODATA, errno);

  // Quarantine xattrs storing the origin and referrer of downloaded files
  // should also be removed.
  ASSERT_FALSE(platform_.GetExtendedFileAttribute(
      kToFilePath, kSourceURLXattrName, nullptr, 0));
  ASSERT_EQ(ENODATA, errno);
  ASSERT_FALSE(platform_.GetExtendedFileAttribute(
      kToFilePath, kReferrerURLXattrName, nullptr, 0));
  ASSERT_EQ(ENODATA, errno);

  // Verify ext2 flags were copied
  int new_ext2_attrs;
  ASSERT_TRUE(platform_.GetExtFileAttributes(kToFilePath, &new_ext2_attrs));
  EXPECT_EQ(ext2_attrs, new_ext2_attrs & ext2_attrs);
}

TEST_F(MigrationHelperTest, CopyOwnership) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  const base::FilePath kLinkTarget = base::FilePath("foo");
  const base::FilePath kLink("link");
  const base::FilePath kFile("file");
  const base::FilePath kDir("dir");
  const base::FilePath kFromLink = from_dir_.Append(kLink);
  const base::FilePath kFromFile = from_dir_.Append(kFile);
  const base::FilePath kFromDir = from_dir_.Append(kDir);
  const base::FilePath kToLink = to_dir_.Append(kLink);
  const base::FilePath kToFile = to_dir_.Append(kFile);
  const base::FilePath kToDir = to_dir_.Append(kDir);
  uid_t file_uid = 1;
  gid_t file_gid = 2;
  uid_t link_uid = 3;
  gid_t link_gid = 4;
  uid_t dir_uid = 5;
  gid_t dir_gid = 6;
  ASSERT_TRUE(platform_.TouchFileDurable(kFromFile));
  ASSERT_TRUE(platform_.CreateSymbolicLink(kFromLink, kLinkTarget));
  ASSERT_TRUE(platform_.CreateDirectory(kFromDir));
  ASSERT_TRUE(platform_.TouchFileDurable(kToFile));
  ASSERT_TRUE(platform_.CreateSymbolicLink(kToLink, kLinkTarget));
  ASSERT_TRUE(platform_.CreateDirectory(kToDir));

  base::stat_wrapper_t stat;
  ASSERT_TRUE(platform_.Stat(kFromFile, &stat));
  stat.st_uid = file_uid;
  stat.st_gid = file_gid;
  EXPECT_CALL(platform_, SetOwnership(kToFile, file_uid, file_gid, false))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      helper.CopyAttributes(kFile, FileEnumerator::FileInfo(kFromFile, stat)));

  ASSERT_TRUE(platform_.Stat(kFromLink, &stat));
  stat.st_uid = link_uid;
  stat.st_gid = link_gid;
  EXPECT_CALL(platform_, SetOwnership(kToLink, link_uid, link_gid, false))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      helper.CopyAttributes(kLink, FileEnumerator::FileInfo(kFromLink, stat)));

  ASSERT_TRUE(platform_.Stat(kFromDir, &stat));
  stat.st_uid = dir_uid;
  stat.st_gid = dir_gid;
  EXPECT_CALL(platform_, SetOwnership(kToDir, dir_uid, dir_gid, false))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      helper.CopyAttributes(kDir, FileEnumerator::FileInfo(kFromDir, stat)));
}

TEST_F(MigrationHelperTest, MigrateNestedDir) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kDir1[] = "directory1";
  constexpr char kDir2[] = "directory2";
  constexpr char kFileName[] = "empty_file";

  // Create directory1/directory2/empty_file in from_dir_.
  ASSERT_TRUE(platform_.CreateDirectory(from_dir_.Append(kDir1).Append(kDir2)));
  ASSERT_TRUE(platform_.TouchFileDurable(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
  ASSERT_TRUE(platform_.IsDirectoryEmpty(to_dir_));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // The file is moved.
  EXPECT_TRUE(platform_.FileExists(
      to_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
  EXPECT_FALSE(platform_.FileExists(
      from_dir_.Append(kDir1).Append(kDir2).Append(kFileName)));
  EXPECT_TRUE(platform_.IsDirectoryEmpty(from_dir_.Append(kDir1)));
}

TEST_F(MigrationHelperTest, MigrateInProgress) {
  // Test the case where the migration was interrupted part way through, but in
  // a clean way such that the two directory trees are consistent (files are
  // only present in one or the other)
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFile1[] = "kFile1";
  constexpr char kFile2[] = "kFile2";
  ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(kFile1)));
  ASSERT_TRUE(platform_.TouchFileDurable(to_dir_.Append(kFile2)));
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // Both files have been moved to to_dir_
  EXPECT_TRUE(platform_.FileExists(to_dir_.Append(kFile1)));
  EXPECT_TRUE(platform_.FileExists(to_dir_.Append(kFile2)));
  EXPECT_FALSE(platform_.FileExists(from_dir_.Append(kFile1)));
  EXPECT_FALSE(platform_.FileExists(from_dir_.Append(kFile2)));
}

TEST_F(MigrationHelperTest, MigrateInProgressDuplicateFile) {
  // Test the case where the migration was interrupted part way through,
  // resulting in files that were successfully written to destination but not
  // yet removed from the source.
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFile1[] = "kFile1";
  constexpr char kFile2[] = "kFile2";
  ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(kFile1)));
  ASSERT_TRUE(platform_.TouchFileDurable(to_dir_.Append(kFile1)));
  ASSERT_TRUE(platform_.TouchFileDurable(to_dir_.Append(kFile2)));
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // Both files have been moved to to_dir_
  EXPECT_TRUE(platform_.FileExists(to_dir_.Append(kFile1)));
  EXPECT_TRUE(platform_.FileExists(to_dir_.Append(kFile2)));
  EXPECT_FALSE(platform_.FileExists(from_dir_.Append(kFile1)));
  EXPECT_FALSE(platform_.FileExists(from_dir_.Append(kFile2)));
}

TEST_F(MigrationHelperTest, MigrateInProgressPartialFile) {
  // Test the case where the migration was interrupted part way through, with a
  // file having been partially copied to the destination but not fully.
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFileName[] = "file";
  const FilePath kFromFilePath = from_dir_.Append(kFileName);
  const FilePath kToFilePath = to_dir_.Append(kFileName);

  const size_t kFinalFileSize = kDefaultChunkSize * 2;
  const size_t kFromFileSize = kDefaultChunkSize;
  const size_t kToFileSize = kDefaultChunkSize;
  char full_contents[kFinalFileSize];
  base::RandBytes(full_contents, kFinalFileSize);

  ASSERT_TRUE(
      platform_.WriteArrayToFile(kFromFilePath, full_contents, kFromFileSize));
  base::File kToFile;
  platform_.InitializeFile(&kToFile, kToFilePath,
                           base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(kToFile.IsValid());
  kToFile.SetLength(kFinalFileSize);
  const size_t kToFileOffset = kFinalFileSize - kToFileSize;
  ASSERT_EQ(
      kToFileSize,
      kToFile.Write(kToFileOffset, full_contents + kToFileOffset, kToFileSize));
  ASSERT_EQ(kFinalFileSize, kToFile.GetLength());
  kToFile.Close();

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // File has been moved to to_dir_
  std::string to_contents;
  ASSERT_TRUE(platform_.ReadFileToString(kToFilePath, &to_contents));
  EXPECT_EQ(std::string(full_contents, kFinalFileSize), to_contents);
  EXPECT_FALSE(platform_.FileExists(kFromFilePath));
}

TEST_F(MigrationHelperTest, MigrateInProgressPartialFileDuplicateData) {
  // Test the case where the migration was interrupted part way through, with a
  // file having been partially copied to the destination but the source file
  // not yet having been truncated to reflect that.
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFileName[] = "file";
  const FilePath kFromFilePath = from_dir_.Append(kFileName);
  const FilePath kToFilePath = to_dir_.Append(kFileName);

  const size_t kFinalFileSize = kDefaultChunkSize * 2;
  const size_t kFromFileSize = kFinalFileSize;
  const size_t kToFileSize = kDefaultChunkSize;
  char full_contents[kFinalFileSize];
  base::RandBytes(full_contents, kFinalFileSize);

  ASSERT_TRUE(
      platform_.WriteArrayToFile(kFromFilePath, full_contents, kFromFileSize));
  base::File kToFile;
  platform_.InitializeFile(&kToFile, kToFilePath,
                           base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(kToFile.IsValid());
  kToFile.SetLength(kFinalFileSize);
  const size_t kToFileOffset = kFinalFileSize - kToFileSize;
  ASSERT_EQ(
      kDefaultChunkSize,
      kToFile.Write(kToFileOffset, full_contents + kToFileOffset, kToFileSize));
  ASSERT_EQ(kFinalFileSize, kToFile.GetLength());
  kToFile.Close();

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // File has been moved to to_dir_
  std::string to_contents;
  ASSERT_TRUE(platform_.ReadFileToString(kToFilePath, &to_contents));
  EXPECT_EQ(std::string(full_contents, kFinalFileSize), to_contents);
  EXPECT_FALSE(platform_.FileExists(kFromFilePath));
}

TEST_F(MigrationHelperTest, ProgressCallback) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFileName[] = "file";
  constexpr char kLinkName[] = "link";
  constexpr char kDirName[] = "dir";
  const FilePath kFromSubdir = from_dir_.Append(kDirName);
  const FilePath kFromFile = kFromSubdir.Append(kFileName);
  const FilePath kFromLink = kFromSubdir.Append(kLinkName);
  const FilePath kToSubdir = to_dir_.Append(kDirName);
  const FilePath kToFile = kToSubdir.Append(kFileName);

  const size_t kFileSize = kDefaultChunkSize;
  char from_contents[kFileSize];
  base::RandBytes(from_contents, kFileSize);
  ASSERT_TRUE(platform_.CreateDirectory(kFromSubdir));
  ASSERT_TRUE(platform_.CreateSymbolicLink(kFromLink, kFromFile.BaseName()));
  ASSERT_TRUE(platform_.WriteArrayToFile(kFromFile, from_contents, kFileSize));
  int64_t expected_size = kFileSize;
  expected_size += kFromFile.BaseName().value().length();
  int64_t dir_size;
  ASSERT_TRUE(platform_.GetFileSize(kFromSubdir, &dir_size));
  expected_size += dir_size;

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  ASSERT_EQ(migrated_values_.size(), total_values_.size());
  int callbacks = migrated_values_.size();
  EXPECT_GT(callbacks, 2);
  EXPECT_EQ(callbacks, total_values_.size());
  EXPECT_EQ(callbacks, status_values_.size());

  // Verify that the progress goes from initializing to in_progress.
  EXPECT_EQ(user_data_auth::DIRCRYPTO_MIGRATION_INITIALIZING,
            status_values_[0]);
  for (int i = 1; i < callbacks; i++) {
    SCOPED_TRACE(i);
    EXPECT_EQ(user_data_auth::DIRCRYPTO_MIGRATION_IN_PROGRESS,
              status_values_[i]);
  }

  // Verify that migrated value starts at 0 and increases to total
  EXPECT_EQ(0, migrated_values_[1]);
  for (int i = 2; i < callbacks - 1; i++) {
    SCOPED_TRACE(i);
    EXPECT_GE(migrated_values_[i], migrated_values_[i - 1]);
  }
  EXPECT_EQ(expected_size, migrated_values_[callbacks - 1]);

  // Verify that total always matches the expected size
  EXPECT_EQ(callbacks, total_values_.size());
  for (int i = 1; i < callbacks; i++) {
    SCOPED_TRACE(i);
    EXPECT_EQ(expected_size, total_values_[i]);
  }
}

TEST_F(MigrationHelperTest, NotEnoughFreeSpace) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(_)).WillOnce(Return(0));
  EXPECT_FALSE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
}

TEST_F(MigrationHelperTest, ForceSmallerChunkSize) {
  constexpr int kMaxChunkSize = 128 << 20;  // 128MB
  constexpr int kNumJobThreads = 2;
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kMaxChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);
  helper.set_num_job_threads_for_testing(kNumJobThreads);

  constexpr int kFreeSpace = 13 << 20;
  // Chunk size should be limited to a multiple of 4MB (kErasureBlockSize)
  // smaller than (kFreeSpace - kFreeSpaceBuffer) / kNumJobThreads (4MB)
  constexpr int kExpectedChunkSize = 4 << 20;
  constexpr int kFileSize = 7 << 20;
  const FilePath kFromFilePath = from_dir_.Append("file");
  base::File from_file;
  platform_.InitializeFile(&from_file, kFromFilePath,
                           base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(from_file.IsValid());

  from_file.SetLength(kFileSize);
  from_file.Close();

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(_)).WillOnce(Return(kFreeSpace));
  EXPECT_CALL(platform_, SendFile(_, _, kExpectedChunkSize,
                                  kFileSize - kExpectedChunkSize))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SendFile(_, _, 0, kExpectedChunkSize))
      .WillOnce(Return(true));
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
}

TEST_F(MigrationHelperTest, SkipInvalidSQLiteFiles) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);
  const char kCorruptedFilePath[] =
      "root/android-data/data/user/0/com.google.android.gms/"
      "databases/playlog.db-shm";
  const FilePath kFromSQLiteShm = from_dir_.Append(kCorruptedFilePath);
  const FilePath kToSQLiteShm = to_dir_.Append(kCorruptedFilePath);
  const FilePath kSkippedFileLog = to_dir_.Append(kSkippedFileListFileName);
  ASSERT_TRUE(platform_.CreateDirectory(kFromSQLiteShm.DirName()));
  ASSERT_TRUE(platform_.TouchFileDurable(kFromSQLiteShm));
  EXPECT_CALL(platform_, InitializeFile(_, _, _)).WillRepeatedly(DoDefault());
  EXPECT_CALL(platform_, InitializeFile(_, kFromSQLiteShm, _))
      .WillOnce(
          Invoke([](base::File* file, const FilePath& path, uint32_t mode) {
            *file = base::File(base::File::FILE_ERROR_IO);
          }));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
  EXPECT_TRUE(platform_.DirectoryExists(kToSQLiteShm.DirName()));
  EXPECT_FALSE(platform_.FileExists(kToSQLiteShm));
  EXPECT_FALSE(platform_.FileExists(kFromSQLiteShm));
  EXPECT_TRUE(platform_.FileExists(kSkippedFileLog));
  std::string contents;
  ASSERT_TRUE(platform_.ReadFileToString(kSkippedFileLog, &contents));
  EXPECT_EQ(std::string(kCorruptedFilePath) + "\n", contents);
}

TEST_F(MigrationHelperTest, AllJobThreadsFailing) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr int kNumJobThreads = 2;
  helper.set_num_job_threads_for_testing(kNumJobThreads);
  helper.set_max_job_list_size_for_testing(1);

  // Create more files than the job threads.
  for (int i = 0; i < kNumJobThreads * 2; ++i) {
    ASSERT_TRUE(platform_.TouchFileDurable(
        from_dir_.AppendASCII(base::NumberToString(i))));
  }
  // All job threads will stop processing jobs because of errors. Also, set
  // errno to avoid confusing base::File::OSErrorToFileError(). crbug.com/731809
  EXPECT_CALL(platform_, DeleteFile(_))
      .WillRepeatedly(SetErrnoAndReturn(EIO, false));
  // Migrate() still returns the result without deadlocking. crbug.com/731575
  EXPECT_FALSE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
}

TEST_F(MigrationHelperTest, SkipDuppedGCacheTmpDir) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  // Prepare the problematic path.
  constexpr char kGCacheV1[] = "user/GCache/v1";
  constexpr char kTmpDir[] = "tmp";
  constexpr char kCachedDir[] = "foobar";
  constexpr char kCachedFile[] = "tmp.gdoc";

  const base::FilePath v1_path(from_dir_.Append(kGCacheV1));
  const base::FilePath cached_dir = v1_path.Append(kTmpDir).Append(kCachedDir);
  const base::FilePath cached_file = cached_dir.Append(kCachedFile);

  ASSERT_TRUE(platform_.CreateDirectory(cached_dir));
  ASSERT_TRUE(platform_.TouchFileDurable(cached_file));

  // Test the migration.
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // Ensure that the inner path is never visited.
  ASSERT_FALSE(platform_.FileExists(v1_path));
  ASSERT_TRUE(platform_.FileExists(to_dir_.Append(kGCacheV1)));
  ASSERT_FALSE(platform_.FileExists(to_dir_.Append(kGCacheV1).Append(kTmpDir)));
}

TEST_F(MigrationHelperTest, MinimalMigration) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::MINIMAL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  std::vector<FilePath> expect_kept_dirs;
  std::vector<FilePath> expect_kept_files;
  std::vector<FilePath> expect_skipped_dirs;
  std::vector<FilePath> expect_skipped_files;

  // Set up expectations about what is skipped and what is kept.
  // Random stuff not on the allowlist is skipped.
  expect_skipped_dirs.emplace_back("user/Application Cache");
  expect_skipped_dirs.emplace_back("root/android-data");
  expect_skipped_files.emplace_back("user/Application Cache/subfile");
  expect_skipped_files.emplace_back("user/skipped_file");
  expect_skipped_files.emplace_back("root/skipped_file");

  // session_manager/policy in the root section is kept along with children.
  expect_kept_dirs.emplace_back("root/session_manager/policy");
  expect_kept_files.emplace_back("root/session_manager/policy/subfile1");
  expect_kept_files.emplace_back("root/session_manager/policy/subfile2");
  expect_kept_dirs.emplace_back("user/log");
  // .pki directory is kept along with contents
  expect_kept_dirs.emplace_back("user/.pki");
  expect_kept_dirs.emplace_back("user/.pki/nssdb");
  expect_kept_files.emplace_back("user/.pki/nssdb/subfile1");
  expect_kept_files.emplace_back("user/.pki/nssdb/subfile2");
  // top-level Web Data is kept
  expect_kept_files.emplace_back("user/Web Data");

  // Create all directories
  for (const auto& path : expect_kept_dirs)
    ASSERT_TRUE(platform_.CreateDirectory(from_dir_.Append(path)))
        << path.value();

  for (const auto& path : expect_skipped_dirs)
    ASSERT_TRUE(platform_.CreateDirectory(from_dir_.Append(path)))
        << path.value();

  // Create all files
  for (const auto& path : expect_kept_files)
    ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(path)))
        << path.value();

  for (const auto& path : expect_skipped_files)
    ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(path)))
        << path.value();

  // Test the minimal migration.
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // Only the expected files and directories are moved.
  for (const auto& path : expect_kept_dirs)
    EXPECT_TRUE(platform_.DirectoryExists(to_dir_.Append(path)))
        << path.value();

  for (const auto& path : expect_kept_files)
    EXPECT_TRUE(platform_.FileExists(to_dir_.Append(path))) << path.value();

  for (const auto& path : expect_skipped_dirs)
    EXPECT_FALSE(platform_.FileExists(to_dir_.Append(path))) << path.value();

  for (const auto& path : expect_skipped_files)
    EXPECT_FALSE(platform_.FileExists(to_dir_.Append(path))) << path.value();

  // The source is empty.
  EXPECT_TRUE(platform_.IsDirectoryEmpty(from_dir_));
}

TEST_F(MigrationHelperTest, CancelMigrationBeforeStart) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  // Cancel migration before starting, and migration just fails.
  helper.Cancel();
  EXPECT_FALSE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
}

TEST_F(MigrationHelperTest, CancelMigrationOnAnotherThread) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  // One empty file to migrate.
  constexpr char kFileName[] = "empty_file";
  ASSERT_TRUE(platform_.TouchFileDurable(from_dir_.Append(kFileName)));
  // Wait in SyncFile so that cancellation happens before migration finishes.
  base::WaitableEvent syncfile_is_called_event(
      base::WaitableEvent::ResetPolicy::AUTOMATIC,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  base::WaitableEvent cancel_is_called_event(
      base::WaitableEvent::ResetPolicy::AUTOMATIC,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  EXPECT_CALL(platform_, SyncFile(to_dir_.Append(kFileName)))
      .WillOnce(InvokeWithoutArgs(
          [&syncfile_is_called_event, &cancel_is_called_event]() {
            syncfile_is_called_event.Signal();
            cancel_is_called_event.Wait();
            return true;
          }));

  // Cancel on another thread after waiting for SyncFile to get called.
  base::Thread thread("Canceller thread");
  ASSERT_TRUE(thread.Start());
  thread.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&base::WaitableEvent::Wait,
                                base::Unretained(&syncfile_is_called_event)));
  thread.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&MigrationHelper::Cancel, base::Unretained(&helper)));
  thread.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&base::WaitableEvent::Signal,
                                base::Unretained(&cancel_is_called_event)));
  // Migration gets cancelled.
  EXPECT_FALSE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));
}

class DataMigrationTest : public MigrationHelperTest,
                          public ::testing::WithParamInterface<size_t> {};

TEST_P(DataMigrationTest, CopyFileData) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);

  constexpr char kFileName[] = "file";
  const FilePath kFromFile = from_dir_.Append(kFileName);
  const FilePath kToFile = to_dir_.Append(kFileName);

  const size_t kFileSize = GetParam();
  char from_contents[kFileSize];
  base::RandBytes(from_contents, kFileSize);
  ASSERT_TRUE(platform_.WriteArrayToFile(kFromFile, from_contents, kFileSize));

  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  std::string to_contents;
  ASSERT_TRUE(platform_.ReadFileToString(kToFile, &to_contents));
  EXPECT_EQ(0, strncmp(from_contents, to_contents.data(), kFileSize));
  EXPECT_FALSE(platform_.FileExists(kFromFile));
}

INSTANTIATE_TEST_SUITE_P(WithRandomData,
                         DataMigrationTest,
                         Values(kDefaultChunkSize / 2,
                                kDefaultChunkSize,
                                kDefaultChunkSize * 2,
                                kDefaultChunkSize * 2 + kDefaultChunkSize / 2,
                                kDefaultChunkSize * 10,
                                kDefaultChunkSize * 100,
                                123456,
                                1,
                                2));

// MigrationHelperJobListTest verifies that the job list size limit doesn't
// cause dead lock, however small (or big) the limit is.
class MigrationHelperJobListTest
    : public MigrationHelperTest,
      public ::testing::WithParamInterface<size_t> {};

TEST_P(MigrationHelperJobListTest, ProcessJobs) {
  MigrationHelper helper(&platform_, from_dir_, to_dir_, status_files_dir_,
                         kDefaultChunkSize, MigrationType::FULL);
  helper.set_namespaced_mtime_xattr_name_for_testing(kMtimeXattrName);
  helper.set_namespaced_atime_xattr_name_for_testing(kAtimeXattrName);
  helper.set_max_job_list_size_for_testing(GetParam());

  // Prepare many files and directories.
  constexpr int kNumDirectories = 100;
  constexpr int kNumFilesPerDirectory = 10;
  for (int i = 0; i < kNumDirectories; ++i) {
    SCOPED_TRACE(i);
    FilePath dir = from_dir_.AppendASCII(base::NumberToString(i));
    ASSERT_TRUE(platform_.CreateDirectory(dir));
    for (int j = 0; j < kNumFilesPerDirectory; ++j) {
      SCOPED_TRACE(j);
      const std::string data =
          base::NumberToString(i * kNumFilesPerDirectory + j);
      ASSERT_TRUE(platform_.WriteStringToFile(
          dir.AppendASCII(base::NumberToString(j)), data));
    }
  }

  // Migrate.
  EXPECT_TRUE(helper.Migrate(base::BindRepeating(
      &MigrationHelperTest::ProgressCaptor, base::Unretained(this))));

  // The files and directories are moved.
  for (int i = 0; i < kNumDirectories; ++i) {
    SCOPED_TRACE(i);
    FilePath dir = to_dir_.AppendASCII(base::NumberToString(i));
    EXPECT_TRUE(platform_.DirectoryExists(dir));
    for (int j = 0; j < kNumFilesPerDirectory; ++j) {
      SCOPED_TRACE(j);
      std::string data;
      EXPECT_TRUE(platform_.ReadFileToString(
          dir.AppendASCII(base::NumberToString(j)), &data));
      EXPECT_EQ(base::NumberToString(i * kNumFilesPerDirectory + j), data);
    }
  }
  EXPECT_TRUE(platform_.IsDirectoryEmpty(from_dir_));
}

INSTANTIATE_TEST_SUITE_P(JobListSize,
                         MigrationHelperJobListTest,
                         Values(1, 10, 100, 1000));

}  // namespace dircrypto_data_migrator
}  // namespace cryptohome
