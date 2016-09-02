// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for cros_disks::MountManager. See mount-manager.h for details
// on MountManager.

#include "cros-disks/mount_manager.h"

#include <sys/mount.h>
#include <sys/unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/metrics.h"
#include "cros-disks/mount_entry.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"

using std::set;
using std::string;
using std::vector;
using testing::ElementsAre;
using testing::Invoke;
using testing::Return;
using testing::_;

namespace {

const char kMountRootDirectory[] = "/media/removable";
const char kTestSourcePath[] = "source";
const char kTestMountPath[] = "/media/removable/test";
const char kInvalidMountPath[] = "/media/removable/../test/doc";

}  // namespace

namespace cros_disks {

// A mock platform class for testing the mount manager base class.
class MockPlatform : public Platform {
 public:
  MockPlatform() {}

  MOCK_CONST_METHOD1(CreateDirectory, bool(const string& path));
  MOCK_CONST_METHOD1(CreateOrReuseEmptyDirectory, bool(const string& path));
  MOCK_CONST_METHOD3(CreateOrReuseEmptyDirectoryWithFallback,
                     bool(string* path, unsigned max_suffix_to_retry,
                          const set<string>& reserved_paths));
  MOCK_CONST_METHOD1(RemoveEmptyDirectory, bool(const string& path));
  MOCK_CONST_METHOD3(SetOwnership, bool(const string& path,
                                        uid_t user_id, gid_t group_id));
  MOCK_CONST_METHOD2(SetPermissions, bool(const string& path, mode_t mode));
};

// A mock mount manager class for testing the mount manager base class.
class MountManagerUnderTest : public MountManager {
 public:
  MountManagerUnderTest(Platform* platform, Metrics* metrics)
      : MountManager(kMountRootDirectory, platform, metrics) {
  }

  MOCK_CONST_METHOD1(CanMount, bool(const string& source_path));
  MOCK_CONST_METHOD0(GetMountSourceType, MountSourceType());
  MOCK_METHOD5(DoMount, MountErrorType(const string& source_path,
                                       const string& filesystem_type,
                                       const vector<string>& options,
                                       const string& mount_path,
                                       MountOptions* applied_options));
  MOCK_METHOD2(DoUnmount, MountErrorType(const string& path,
                                         const vector<string>& options));
  MOCK_CONST_METHOD1(ShouldReserveMountPathOnError,
                     bool(MountErrorType error_type));
  MOCK_CONST_METHOD1(SuggestMountPath, string(const string& source_path));
};

class MountManagerTest : public ::testing::Test {
 public:
  MountManagerTest() : manager_(&platform_, &metrics_) {}

 protected:
  Metrics metrics_;
  MockPlatform platform_;
  MountManagerUnderTest manager_;
  string filesystem_type_;
  string mount_path_;
  string source_path_;
  vector<string> options_;
};

// Verifies that MountManager::Initialize() returns false when it fails to
// create the mount root directory.
TEST_F(MountManagerTest, InitializeFailedInCreateDirectory) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_,
              SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .Times(0);
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _))
      .Times(0);

  EXPECT_FALSE(manager_.Initialize());
}

// Verifies that MountManager::Initialize() returns false when it fails to
// set the ownership of the created mount root directory.
TEST_F(MountManagerTest, InitializeFailedInSetOwnership) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _))
      .Times(0);

  EXPECT_FALSE(manager_.Initialize());
}

// Verifies that MountManager::Initialize() returns false when it fails to
// set the permissions of the created mount root directory.
TEST_F(MountManagerTest, InitializeFailedInSetPermissions) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(manager_.Initialize());
}

// Verifies that MountManager::Initialize() returns true when it creates
// the mount root directory with the specified ownership and permissions.
TEST_F(MountManagerTest, InitializeSucceeded) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(manager_.Initialize());
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// to mount an empty source path.
TEST_F(MountManagerTest, MountFailedWithEmptySourcePath) {
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_INVALID_ARGUMENT,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// with a nullptr mount path.
TEST_F(MountManagerTest, MountFailedWithNullMountPath) {
  source_path_ = kTestSourcePath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_INVALID_ARGUMENT,
            manager_.Mount(source_path_, filesystem_type_, options_, nullptr));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// with an invalid mount path.
TEST_F(MountManagerTest, MountFailedWithInvalidMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kInvalidMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_INVALID_PATH,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// without a given mount path and the suggested mount path is invalid.
TEST_F(MountManagerTest, MountFailedWithInvalidSuggestedMountPath) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kInvalidMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_))
      .WillRepeatedly(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_INVALID_PATH,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));

  options_.push_back("mountlabel=custom_label");
  EXPECT_EQ(MOUNT_ERROR_INVALID_PATH,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// with an mount label that yields an invalid mount path.
TEST_F(MountManagerTest, MountFailedWithInvalidMountLabel) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestSourcePath;
  options_.push_back("mountlabel=../custom_label");

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_INVALID_PATH,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// create the specified mount directory.
TEST_F(MountManagerTest, MountFailedInCreateOrReuseEmptyDirectory) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// create a specified but already reserved mount directory.
TEST_F(MountManagerTest, MountFailedInCreateDirectoryDueToReservedMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// create a mount directory after a number of trials.
TEST_F(MountManagerTest, MountFailedInCreateOrReuseEmptyDirectoryWithFallback) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ("", mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(suggested_mount_path));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// set the ownership of the created mount directory.
TEST_F(MountManagerTest, MountFailedInSetOwnership) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, SetPermissions(_, _)).Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// set the permissions of the created mount directory.
TEST_F(MountManagerTest, MountFailedInSetPermissions) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Mount() returns no error when it successfully
// mounts a source path to a specified mount path.
TEST_F(MountManagerTest, MountSucceededWithGivenMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, DoUnmount(mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));

  MountManager::MountState mount_state;
  manager_.GetMountStateFromCache(source_path_, &mount_state);
  EXPECT_TRUE(mount_state.is_read_only);
}

// Mock action to emulate DoMount with fallback to read-only mode.
MountErrorType DoMountSuccessReadOnly(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& options,
    const std::string& mount_path,
    MountOptions* applied_options) {
  applied_options->SetReadOnlyOption();
  return MOUNT_ERROR_NONE;
}

// Verifies that MountManager::Mount() stores corret mount status in cache when
// read-only option is specified.
TEST_F(MountManagerTest, MountCachesStatusWithReadOnlyOption) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  // Add read-only mount option.
  options_.push_back("ro");
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  MountManager::MountState mount_state;
  manager_.GetMountStateFromCache(source_path_, &mount_state);
  EXPECT_TRUE(mount_state.is_read_only);
}

// Verifies that MountManager::Mount() stores corret mount status in cache when
// the mounter successfully mounted a device but only in its read-only mode.
TEST_F(MountManagerTest, MountSuccededWithReadOnlyFallback) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  // Emulate Mounter added read-only option as a fallback.
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Invoke(DoMountSuccessReadOnly));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  MountManager::MountState mount_state;
  manager_.GetMountStateFromCache(source_path_, &mount_state);
  EXPECT_TRUE(mount_state.is_read_only);
}

// Verifies that MountManager::Mount() returns no error when it successfully
// mounts a source path with no mount path specified.
TEST_F(MountManagerTest, MountSucceededWithEmptyMountPath) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(suggested_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(suggested_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, DoUnmount(suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() returns no error when it successfully
// mounts a source path with a given mount label in options.
TEST_F(MountManagerTest, MountSucceededWithGivenMountLabel) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;
  string final_mount_path = string(kMountRootDirectory) + "/custom_label";
  options_.push_back("mountlabel=custom_label");
  vector<string> updated_options;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(final_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(final_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(final_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, updated_options,
                                final_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, DoUnmount(final_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(final_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() handles the mounting of an already
// mounted source path properly.
TEST_F(MountManagerTest, MountWithAlreadyMountedSourcePath) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(suggested_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(suggested_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, DoUnmount(suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  // Mount an already-mounted source path without specifying a mount path
  mount_path_.clear();
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  // Mount an already-mounted source path to the same mount path
  mount_path_ = suggested_mount_path;
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  // Mount an already-mounted source path to a different mount path
  mount_path_ = "another-path";
  EXPECT_EQ(MOUNT_ERROR_PATH_ALREADY_MOUNTED,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathInCache(suggested_mount_path));

  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(suggested_mount_path));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error. A specific mount path is given in this case.
TEST_F(MountManagerTest, MountSucceededWithGivenMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error. No specific mount path is given in this case.
TEST_F(MountManagerTest, MountSucceededWithEmptyMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(suggested_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(suggested_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error and returns the same error when it tries to mount the same path
// again.
TEST_F(MountManagerTest, MountSucceededWithAlreadyReservedMountPath) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(suggested_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(suggested_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  mount_path_ = "";
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error and returns the same error when it tries to mount the same path
// again.
TEST_F(MountManagerTest, MountFailedWithGivenMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(false));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() fails to mount or reserve a path for
// a type of error that is not enabled for reservation.
TEST_F(MountManagerTest, MountFailedWithEmptyMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(suggested_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(suggested_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                suggested_mount_path, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(false));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ("", mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Unmount() returns an error when it is invoked
// to unmount an empty path.
TEST_F(MountManagerTest, UnmountFailedWithEmptyPath) {
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_INVALID_ARGUMENT,
            manager_.Unmount(mount_path_, options_));
}

// Verifies that MountManager::Unmount() returns an error when it fails to
// unmount a path that is not mounted.
TEST_F(MountManagerTest, UnmountFailedWithPathNotMounted) {
  mount_path_ = "nonexistent-path";

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, DoUnmount(_, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED,
            manager_.Unmount(mount_path_, options_));
}

// Verifies that MountManager::Unmount() returns no error when it successfully
// unmounts a source path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenSourcePath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, DoUnmount(mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(source_path_, options_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it successfully
// unmounts a mount path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, DoUnmount(mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(mount_path_, options_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it is invoked
// to unmount the source path of a reserved mount path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenSourcePathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(mount_path_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(source_path_, options_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it is invoked
// to unmount a reserved mount path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SetOwnership(mount_path_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(mount_path_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                mount_path_, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_, DoUnmount(mount_path_, _)).Times(0);
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.Mount(source_path_, filesystem_type_, options_,
                           &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(mount_path_, options_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::AddMountPathToCache() works as expected.
TEST_F(MountManagerTest, AddMountPathToCache) {
  string result;
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;
  bool is_read_only = true;

  EXPECT_TRUE(manager_.AddMountPathToCache(source_path_, mount_path_,
                                           is_read_only));
  EXPECT_TRUE(manager_.GetMountPathFromCache(source_path_, &result));
  EXPECT_EQ(mount_path_, result);
  MountManager::MountState result_state;
  EXPECT_TRUE(manager_.GetMountStateFromCache(source_path_, &result_state));
  EXPECT_EQ(kTestMountPath, result_state.mount_path);
  EXPECT_EQ(is_read_only, result_state.is_read_only);

  EXPECT_FALSE(manager_.AddMountPathToCache(source_path_, "target1", false));
  EXPECT_TRUE(manager_.GetMountPathFromCache(source_path_, &result));
  EXPECT_EQ(mount_path_, result);

  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
}

// Verifies that MountManager::GetSourcePathFromCache() works as expected.
TEST_F(MountManagerTest, GetSourcePathFromCache) {
  string result;
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.GetSourcePathFromCache(mount_path_, &result));
  EXPECT_TRUE(manager_.AddMountPathToCache(source_path_, mount_path_, false));
  EXPECT_TRUE(manager_.GetSourcePathFromCache(mount_path_, &result));
  EXPECT_EQ(source_path_, result);
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_FALSE(manager_.GetSourcePathFromCache(mount_path_, &result));
}

// Verifies that MountManager::GetMountPathFromCache() works as expected.
TEST_F(MountManagerTest, GetMountPathFromCache) {
  string result;
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.GetMountPathFromCache(source_path_, &result));
  EXPECT_TRUE(manager_.AddMountPathToCache(source_path_, mount_path_, false));
  EXPECT_TRUE(manager_.GetMountPathFromCache(source_path_, &result));
  EXPECT_EQ(mount_path_, result);
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_FALSE(manager_.GetMountPathFromCache(source_path_, &result));
}

// Verifies that MountManager::IsMountPathInCache() works as expected.
TEST_F(MountManagerTest, IsMountPathInCache) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.AddMountPathToCache(source_path_, mount_path_, false));
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::RemoveMountPathFromCache() works as expected.
TEST_F(MountManagerTest, RemoveMountPathFromCache) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_TRUE(manager_.AddMountPathToCache(source_path_, mount_path_, false));
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_FALSE(manager_.RemoveMountPathFromCache(mount_path_));
}

// Verifies that MountManager::GetReservedMountPaths() works as expected.
TEST_F(MountManagerTest, GetReservedMountPaths) {
  set<string> reserved_paths;
  set<string> expected_paths;
  string path1 = "path1";
  string path2 = "path2";

  reserved_paths = manager_.GetReservedMountPaths();
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.ReserveMountPath(path1, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.insert(path1);
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.ReserveMountPath(path2, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.insert(path2);
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.UnreserveMountPath(path1);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.erase(path1);
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.UnreserveMountPath(path2);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.erase(path2);
  EXPECT_TRUE(expected_paths == reserved_paths);
}

// Verifies that MountManager::ReserveMountPath() and
// MountManager::UnreserveMountPath() work as expected.
TEST_F(MountManagerTest, ReserveAndUnreserveMountPath) {
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.UnreserveMountPath(mount_path_);
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));

  // Removing a nonexistent mount path should be ok
  manager_.UnreserveMountPath(mount_path_);
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));

  // Adding an existent mount path should be ok
  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNSUPPORTED_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNSUPPORTED_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNSUPPORTED_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.UnreserveMountPath(mount_path_);
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
}

// Verifies that MountManager::GetMountEntries() returns the expected list of
// mount entries under different scenarios.
TEST_F(MountManagerTest, GetMountEntries) {
  EXPECT_CALL(manager_, GetMountSourceType())
      .WillRepeatedly(Return(MOUNT_SOURCE_REMOVABLE_DEVICE));

  vector<MountEntry> mount_entries;

  // No mount entries is returned.
  manager_.GetMountEntries(&mount_entries);
  EXPECT_TRUE(mount_entries.empty());

  // Verify that |mount_entries| is overwritten.
  mount_entries.push_back(
      MountEntry(MOUNT_ERROR_NONE, "", MOUNT_SOURCE_ARCHIVE, "", false));
  manager_.GetMountEntries(&mount_entries);
  EXPECT_TRUE(mount_entries.empty());

  // A normal mount entry is returned.
  EXPECT_TRUE(manager_.AddMountPathToCache(kTestSourcePath, kTestMountPath,
                                           false));
  manager_.GetMountEntries(&mount_entries);
  ASSERT_EQ(1, mount_entries.size());
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_entries[0].error_type());
  EXPECT_EQ(kTestSourcePath, mount_entries[0].source_path());
  EXPECT_EQ(MOUNT_SOURCE_REMOVABLE_DEVICE, mount_entries[0].source_type());
  EXPECT_EQ(kTestMountPath, mount_entries[0].mount_path());

  // A reserved mount entry is returned.
  manager_.ReserveMountPath(kTestMountPath, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  manager_.GetMountEntries(&mount_entries);
  ASSERT_EQ(1, mount_entries.size());
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM, mount_entries[0].error_type());
  EXPECT_EQ(kTestSourcePath, mount_entries[0].source_path());
  EXPECT_EQ(MOUNT_SOURCE_REMOVABLE_DEVICE, mount_entries[0].source_type());
  EXPECT_EQ(kTestMountPath, mount_entries[0].mount_path());
}

// Verifies that MountManager::ExtractMountLabelFromOptions() extracts a mount
// label from the given options and returns true.
TEST_F(MountManagerTest, ExtractMountLabelFromOptions) {
  vector<string> options = {"ro", "mountlabel=My USB Drive", "noexec"};
  string mount_label;

  EXPECT_TRUE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro", "noexec"));
  EXPECT_EQ("My USB Drive", mount_label);
}

// Verifies that MountManager::ExtractMountLabelFromOptions() returns false
// when no mount label is found in the given options.
TEST_F(MountManagerTest, ExtractMountLabelFromOptionsWithNoMountLabel) {
  vector<string> options;
  string mount_label;

  EXPECT_FALSE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre());
  EXPECT_EQ("", mount_label);

  options.push_back("ro");
  EXPECT_FALSE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro"));
  EXPECT_EQ("", mount_label);

  options.push_back("mountlabel");
  EXPECT_FALSE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro", "mountlabel"));
  EXPECT_EQ("", mount_label);
}

// Verifies that MountManager::ExtractMountLabelFromOptions() extracts the last
// mount label from the given options with two mount labels.
TEST_F(MountManagerTest, ExtractMountLabelFromOptionsWithTwoMountLabels) {
  vector<string> options = {"ro", "mountlabel=My USB Drive", "noexec",
                            "mountlabel=Another Label"};
  string mount_label;

  EXPECT_TRUE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro", "noexec"));
  EXPECT_EQ("Another Label", mount_label);
}

// Verifies that MountManager::ExtractUnmountOptions() extracts supported
// unmount options and returns true.
TEST_F(MountManagerTest, ExtractSupportedUnmountOptions) {
  int unmount_flags = 0;
  int expected_unmount_flags = MNT_FORCE;
  options_.push_back("force");
  EXPECT_TRUE(manager_.ExtractUnmountOptions(options_, &unmount_flags));
  EXPECT_EQ(expected_unmount_flags, unmount_flags);

  unmount_flags = 0;
  expected_unmount_flags = MNT_DETACH;
  options_.clear();
  options_.push_back("lazy");
  EXPECT_TRUE(manager_.ExtractUnmountOptions(options_, &unmount_flags));
  EXPECT_EQ(expected_unmount_flags, unmount_flags);

  unmount_flags = 0;
  expected_unmount_flags = MNT_FORCE | MNT_DETACH;
  options_.clear();
  options_.push_back("force");
  options_.push_back("lazy");
  EXPECT_TRUE(manager_.ExtractUnmountOptions(options_, &unmount_flags));
  EXPECT_EQ(expected_unmount_flags, unmount_flags);
}

// Verifies that MountManager::ExtractUnmountOptions() returns false when
// unsupported unmount options are given.
TEST_F(MountManagerTest, ExtractUnsupportedUnmountOptions) {
  int unmount_flags = 0;
  options_.push_back("foo");
  EXPECT_FALSE(manager_.ExtractUnmountOptions(options_, &unmount_flags));
  EXPECT_EQ(0, unmount_flags);
}

// Verifies that MountManager::IsPathImmediateChildOfParent() correctly
// determines if a path is an immediate child of another path.
TEST_F(MountManagerTest, IsPathImmediateChildOfParent) {
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      "/media/archive/test.zip", "/media/archive"));
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      "/media/archive/test.zip/", "/media/archive"));
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      "/media/archive/test.zip", "/media/archive/"));
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      "/media/archive/test.zip/", "/media/archive/"));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      "/media/archive/test.zip/doc.zip", "/media/archive/"));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      "/media/archive/test.zip", "/media/removable"));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      "/tmp/archive/test.zip", "/media/removable"));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      "/media", "/media/removable"));
}

// Verifies that MountManager::IsValidMountPath() correctly determines if a
// mount path is an immediate child of the mount root.
TEST_F(MountManagerTest, IsValidMountPath) {
  manager_.mount_root_ = "/media/removable";
  EXPECT_TRUE(manager_.IsValidMountPath("/media/removable/test"));
  EXPECT_TRUE(manager_.IsValidMountPath("/media/removable/test/"));
  EXPECT_TRUE(manager_.IsValidMountPath("/media/removable/test/"));
  EXPECT_TRUE(manager_.IsValidMountPath("/media/removable//test"));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/archive/test"));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/removable/test/doc"));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/removable/../test"));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/removable/../test/"));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/removable/test/.."));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/removable/test/../"));

  manager_.mount_root_ = "/media/archive";
  EXPECT_TRUE(manager_.IsValidMountPath("/media/archive/test"));
  EXPECT_FALSE(manager_.IsValidMountPath("/media/removable/test"));
}

}  // namespace cros_disks
