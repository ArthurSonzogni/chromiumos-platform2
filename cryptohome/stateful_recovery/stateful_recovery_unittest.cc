// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/stateful_recovery/stateful_recovery.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <brillo/cryptohome.h>
#include <user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h>
#include <policy/mock_device_policy.h>
#include <policy/mock_libpolicy.h>

#include "cryptohome/mock_platform.h"

namespace cryptohome {

using base::FilePath;
using std::ostringstream;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrEq;

// StatefulRecoveryTest is a test fixture for all Stateful Recovery unit tests.
class StatefulRecoveryTest : public ::testing::Test {
 public:
  StatefulRecoveryTest() {}

  ~StatefulRecoveryTest() override = default;

  void SetUp() override {
    platform_.reset(new MockPlatform());
    userdataauth_proxy_.reset(new testing::StrictMock<
                              org::chromium::UserDataAuthInterfaceProxyMock>());
    policy_provider_.reset(
        new testing::StrictMock<policy::MockPolicyProvider>());
  }

  void Initialize() {
    recovery_.reset(new StatefulRecovery(platform_.get(),
                                         userdataauth_proxy_.get(),
                                         policy_provider_.get(), flag_file_));
  }

 protected:
  // Mock platform object.
  std::unique_ptr<MockPlatform> platform_;

  // Mock UserData Authentication interface.
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyMock>
      userdataauth_proxy_;

  std::unique_ptr<testing::StrictMock<policy::MockPolicyProvider>>
      policy_provider_;
  testing::StrictMock<policy::MockDevicePolicy> device_policy_;

  // The Stateful Recovery that we want to test.
  std::unique_ptr<StatefulRecovery> recovery_;

  // Location of the flag_file
  const std::string flag_file_ = "mylocalflagfile";

  void PrepareMountRequest(bool success) {
    // Mount
    {
      EXPECT_CALL(*userdataauth_proxy_, Mount)
          .WillOnce([success](auto in_request, auto out_reply,
                              brillo::ErrorPtr* error, int timeout_ms) {
            if (success)
              out_reply->set_error(user_data_auth::CryptohomeErrorCode::
                                       CRYPTOHOME_ERROR_NOT_SET);
            else
              out_reply->set_error(user_data_auth::CryptohomeErrorCode::
                                       CRYPTOHOME_ERROR_MOUNT_FATAL);

            return true;
          });
    }

    // UnMount
    if (success) {
      EXPECT_CALL(*userdataauth_proxy_, Unmount)
          .WillRepeatedly([](auto in_request, auto out_reply,
                             brillo::ErrorPtr* error, int timeout_ms) {
            out_reply->set_error(
                user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET);
            return true;
          });
    }
  }

  void ExpectGetOwner(std::string owner) {
    EXPECT_CALL(*policy_provider_, Reload()).WillOnce(Return(true));
    EXPECT_CALL(*policy_provider_, device_policy_is_loaded())
        .WillOnce(Return(true));
    EXPECT_CALL(*policy_provider_, GetDevicePolicy())
        .WillOnce(ReturnRef(device_policy_));
    EXPECT_CALL(device_policy_, GetOwner(_))
        .WillOnce(DoAll(SetArgPointee<0>(owner), Return(true)));
  }
};

TEST_F(StatefulRecoveryTest, ValidRequestV1) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV1WriteProtected) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string obfuscated_user =
      brillo::cryptohome::home::SanitizeUserName(user);
  FilePath mount_path =
      FilePath("/home/.shadow/").Append(obfuscated_user).Append("mount");
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  auto mock_mount_response = std::make_unique<user_data_auth::MountReply>();

  EXPECT_CALL(*platform_,
              Copy(mount_path, FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  PrepareMountRequest(true);
  ExpectGetOwner(user);
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  // CopyPartitionInfo
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2NotOwner) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string obfuscated_user =
      brillo::cryptohome::home::SanitizeUserName(user);
  FilePath mount_path =
      FilePath("/home/.shadow/").Append(obfuscated_user).Append("mount");
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  EXPECT_CALL(*platform_,
              Copy(mount_path, FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  PrepareMountRequest(true);
  ExpectGetOwner("notuser");
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2BadUser) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  PrepareMountRequest(false);
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2BadUserNotWriteProtected) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  PrepareMountRequest(false);
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));

  // CopyPartitionInfo
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2NotOwnerNotWriteProtected) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string obfuscated_user =
      brillo::cryptohome::home::SanitizeUserName(user);
  FilePath mount_path =
      FilePath("/home/.shadow/").Append(obfuscated_user).Append("mount");
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  PrepareMountRequest(true);
  EXPECT_CALL(*platform_,
              Copy(mount_path, FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  ExpectGetOwner("notowner");
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));

  // CopyPartitionInfo
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, InvalidFlagFileContents) {
  std::string flag_content = "0 hello";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  Initialize();
  EXPECT_FALSE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, UnreadableFlagFile) {
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(Return(false));
  Initialize();
  EXPECT_FALSE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, UncopyableData) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, DirectoryCreationFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, StatVFSFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, FilesystemDetailsFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, UsageReportOk) {
  Platform platform;

  struct statvfs vfs;
  /* Reporting on a valid location produces output. */
  EXPECT_TRUE(platform_->StatVFS(FilePath("/"), &vfs));
  EXPECT_NE(vfs.f_blocks, 0);

  /* Reporting on an invalid location fails. */
  EXPECT_FALSE(platform_->StatVFS(FilePath("/this/is/very/wrong"), &vfs));

  /* TODO(keescook): mockable tune2fs, since it's not installed in chroot. */
}

TEST_F(StatefulRecoveryTest, DestinationRecreateFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_, ReadFileToString(FilePath(flag_file_), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_, DeletePathRecursively(
                              FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(false));
  EXPECT_CALL(*platform_,
              Copy(_, FilePath(StatefulRecovery::kRecoverDestination)))
      .Times(0);

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

}  // namespace cryptohome
