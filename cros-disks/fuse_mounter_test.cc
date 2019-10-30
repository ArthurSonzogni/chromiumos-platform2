// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mounter.h"

#include <memory>
#include <string>
#include <vector>

#include <brillo/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"

namespace cros_disks {

namespace {

using testing::_;
using testing::ElementsAre;
using testing::Invoke;
using testing::Return;
using testing::StartsWith;

const uid_t kMountUID = 200;
const gid_t kMountGID = 201;
const uid_t kFilesUID = 700;
const uid_t kFilesGID = 701;
const uid_t kFilesAccessGID = 1501;
const char kMountUser[] = "fuse-fuse";
const char kFUSEType[] = "fuse";
const char kMountProgram[] = "dummy";
const char kSomeSource[] = "/some/src";
const char kMountDir[] = "/mnt";

// Mock Platform implementation for testing.
class MockFUSEPlatform : public Platform {
 public:
  MockFUSEPlatform() {
    ON_CALL(*this, GetUserAndGroupId(_, _, _))
        .WillByDefault(Invoke(this, &MockFUSEPlatform::GetUserAndGroupIdImpl));
    ON_CALL(*this, GetGroupId(_, _))
        .WillByDefault(Invoke(this, &MockFUSEPlatform::GetGroupIdImpl));
    ON_CALL(*this, PathExists(_)).WillByDefault(Return(true));
    ON_CALL(*this, SetOwnership(_, _, _)).WillByDefault(Return(true));
    ON_CALL(*this, SetPermissions(_, _)).WillByDefault(Return(true));
  }

  MOCK_METHOD(bool,
              GetUserAndGroupId,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              GetGroupId,
              (const std::string&, gid_t*),
              (const, override));
  MOCK_METHOD(MountErrorType,
              Mount,
              (const std::string&,
               const std::string&,
               const std::string&,
               uint64_t,
               const std::string&),
              (const, override));
  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string&, int),
              (const, override));
  MOCK_METHOD(bool, PathExists, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              RemoveEmptyDirectory,
              (const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              SetOwnership,
              (const std::string&, uid_t, gid_t),
              (const, override));
  MOCK_METHOD(bool,
              GetOwnership,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const std::string&, mode_t),
              (const, override));

 private:
  bool GetUserAndGroupIdImpl(const std::string& user,
                             uid_t* user_id,
                             gid_t* group_id) const {
    if (user == "chronos") {
      if (user_id)
        *user_id = kFilesUID;
      if (group_id)
        *group_id = kFilesGID;
      return true;
    }
    if (user == kMountUser) {
      if (user_id)
        *user_id = kMountUID;
      if (group_id)
        *group_id = kMountGID;
      return true;
    }
    return false;
  }

  bool GetGroupIdImpl(const std::string& group, gid_t* group_id) const {
    if (group == "chronos-access") {
      if (group_id)
        *group_id = kFilesAccessGID;
      return true;
    }
    return false;
  }
};

class MockSandboxedProcess : public SandboxedProcess {
 public:
  MockSandboxedProcess() = default;
  MOCK_METHOD(pid_t,
              StartImpl,
              (base::ScopedFD, base::ScopedFD, base::ScopedFD),
              (override));
  MOCK_METHOD(int, WaitImpl, (), (override));
  MOCK_METHOD(int, WaitNonBlockingImpl, (), (override));
};

class FUSEMounterForTesting : public FUSEMounter {
 public:
  FUSEMounterForTesting(const Platform* platform,
                        brillo::ProcessReaper* process_reaper)
      : FUSEMounter(kSomeSource,
                    kMountDir,
                    kFUSEType,
                    MountOptions(),
                    platform,
                    process_reaper,
                    kMountProgram,
                    kMountUser,
                    {},
                    {},
                    false) {}

  MOCK_METHOD(int, InvokeMountTool, (const std::vector<std::string>&), (const));

 private:
  std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const override {
    auto mock = std::make_unique<MockSandboxedProcess>();
    auto* raw_ptr = mock.get();
    ON_CALL(*mock, StartImpl(_, _, _)).WillByDefault(Return(123));
    ON_CALL(*mock, WaitNonBlockingImpl())
        .WillByDefault(Invoke([raw_ptr, this]() {
          return InvokeMountTool(raw_ptr->arguments());
        }));
    return mock;
  }
};

}  // namespace

TEST(FUSEMounterTest, Sandboxing_Unprivileged) {
  MockFUSEPlatform platform;
  brillo::ProcessReaper process_reaper;
  FUSEMounterForTesting mounter(&platform, &process_reaper);
  EXPECT_CALL(mounter, InvokeMountTool(ElementsAre(
                           kMountProgram, "-o", MountOptions().ToString(),
                           kSomeSource, StartsWith("/dev/fd/"))))
      .WillOnce(Return(0));

  // Expectations for sandbox setup.
  EXPECT_CALL(platform, Mount(kSomeSource, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  EXPECT_CALL(platform, PathExists(kMountProgram)).WillOnce(Return(true));
  EXPECT_CALL(platform, PathExists(kSomeSource)).WillOnce(Return(true));
  EXPECT_CALL(platform, SetOwnership(kSomeSource, getuid(), kMountGID))
      .WillOnce(Return(true));
  EXPECT_CALL(platform, SetPermissions(kSomeSource,
                                       S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP))
      .WillOnce(Return(true));
  EXPECT_CALL(platform, SetOwnership(kMountDir, _, _)).Times(0);
  EXPECT_CALL(platform, SetPermissions(kMountDir, _)).Times(0);
  EXPECT_CALL(platform, Unmount(_, _)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, mounter.Mount());
}

TEST(FUSEMounterTest, AppFailed) {
  MockFUSEPlatform platform;
  brillo::ProcessReaper process_reaper;
  FUSEMounterForTesting mounter(&platform, &process_reaper);

  EXPECT_CALL(platform, Mount(kSomeSource, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform, Unmount(_, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform, RemoveEmptyDirectory(_)).WillOnce(Return(true));
  EXPECT_CALL(mounter, InvokeMountTool(_)).WillOnce(Return(1));

  EXPECT_EQ(MOUNT_ERROR_MOUNT_PROGRAM_FAILED, mounter.Mount());
}

}  // namespace cros_disks
