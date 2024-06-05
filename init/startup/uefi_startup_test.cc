// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/uefi_startup.h"

#include <fcntl.h>
#include <sys/mount.h>

// The include for sys/mount.h must come before this.
#include <linux/fs.h>

#include <memory>

#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>

#include "init/startup/constants.h"
#include "init/startup/uefi_startup_impl.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace startup {

bool operator==(const UefiDelegate::UserAndGroup& lhs,
                const UefiDelegate::UserAndGroup& rhs) {
  return lhs.uid == rhs.uid && lhs.gid == rhs.gid;
}

class MockUefiDelegate : public UefiDelegate {
 public:
  MockUefiDelegate() = default;

  MOCK_METHOD(bool, IsUefiEnabled, (), (const, override));
  MOCK_METHOD(std::optional<UserAndGroup>,
              GetFwupdUserAndGroup,
              (),
              (const, override));
  MOCK_METHOD(bool, MountEfivarfs, (const UserAndGroup& fwupd), (override));
  MOCK_METHOD(bool,
              MakeUefiVarMutable,
              (const std::string& vendor, const std::string& name),
              (override));
  MOCK_METHOD(void,
              MakeEsrtReadableByFwupd,
              (const UserAndGroup& fwupd),
              (override));
};

// Test that the appropriate actions are taken if UEFI is enabled.
TEST(UefiStartup, UefiEnabled) {
  StrictMock<MockUefiDelegate> mock_uefi_delegate;

  UefiDelegate::UserAndGroup fwupd{1, 2};

  EXPECT_CALL(mock_uefi_delegate, IsUefiEnabled()).WillOnce(Return(true));
  EXPECT_CALL(mock_uefi_delegate, GetFwupdUserAndGroup())
      .WillOnce(Return(fwupd));
  EXPECT_CALL(mock_uefi_delegate,
              MountEfivarfs(UefiDelegate::UserAndGroup{1, 2}))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_uefi_delegate,
              MakeUefiVarMutable(kEfiImageSecurityDatabaseGuid, "dbx"))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_uefi_delegate, MakeEsrtReadableByFwupd(fwupd));

  MaybeRunUefiStartup(mock_uefi_delegate);
}

// Test that nothing happens if UEFI is not enabled.
TEST(UefiStartup, UefiDisabled) {
  StrictMock<MockUefiDelegate> mock_uefi_delegate;

  EXPECT_CALL(mock_uefi_delegate, IsUefiEnabled()).WillOnce(Return(false));

  MaybeRunUefiStartup(mock_uefi_delegate);
}

class UefiDelegateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    uefi_delegate_ = std::make_unique<UefiDelegateImpl>(&platform_, root_dir_);
  }

  libstorage::MockPlatform platform_;
  base::FilePath root_dir_{"/"};

  std::unique_ptr<UefiDelegate> uefi_delegate_;
};

// Test `IsUefiEnabled` when UEFI is enabled.
TEST_F(UefiDelegateTest, IsUefiEnabledYes) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  ASSERT_TRUE(platform_.CreateDirectory(efivars_dir));

  EXPECT_TRUE(uefi_delegate_->IsUefiEnabled());
}

// Test `IsUefiEnabled` when UEFI is not enabled.
TEST_F(UefiDelegateTest, IsUefiEnabledNo) {
  const base::FilePath efivars_dir = root_dir_.Append("sys/firmware");
  ASSERT_TRUE(platform_.CreateDirectory(efivars_dir));

  EXPECT_FALSE(uefi_delegate_->IsUefiEnabled());
}

// Test mounting efivarfs.
TEST_F(UefiDelegateTest, MountEfivarfs) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  ASSERT_TRUE(platform_.CreateDirectory(efivars_dir));

  EXPECT_CALL(platform_, Mount(base::FilePath(), efivars_dir, kFsTypeEfivarfs,
                               kCommonMountFlags, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(
      uefi_delegate_->MountEfivarfs(UefiDelegate::UserAndGroup{123, 456}));
}

// Test modifying a UEFI var.
TEST_F(UefiDelegateTest, ModifyVar) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  ASSERT_TRUE(platform_.CreateDirectory(efivars_dir));

  const base::FilePath var_path =
      efivars_dir.Append("myvar-1a2a2d4e-6e6a-468f-944c-c00d14d92c1e");
  ASSERT_TRUE(platform_.WriteStringToFile(var_path, ""));

  EXPECT_TRUE(uefi_delegate_->MakeUefiVarMutable(
      "1a2a2d4e-6e6a-468f-944c-c00d14d92c1e", "myvar"));
}

// Test modifying a UEFI var that doesn't exist.
TEST_F(UefiDelegateTest, ModifyInvalidVar) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  const base::FilePath var_path =
      efivars_dir.Append("myvar-1a2a2d4e-6e6a-468f-944c-c00d14d92c1e");

  EXPECT_FALSE(uefi_delegate_->MakeUefiVarMutable(
      "1a2a2d4e-6e6a-468f-944c-c00d14d92c1e", "myvar"));
}

// Test making the ESRT readable by fwupd.
TEST_F(UefiDelegateTest, MakeEsrtReadableByFwupd) {
  // Set up an esrt directory.
  const base::FilePath esrt_dir = root_dir_.Append(kSysEfiDir).Append("esrt");
  ASSERT_TRUE(platform_.CreateDirectory(esrt_dir));
  const base::FilePath version_path = esrt_dir.Append("fw_resource_version");
  ASSERT_TRUE(platform_.WriteStringToFile(version_path, "1"));
  const base::FilePath entries_dir = esrt_dir.Append("entries");
  ASSERT_TRUE(platform_.CreateDirectory(entries_dir));
  const base::FilePath entry_path = entries_dir.Append("entry_file");
  ASSERT_TRUE(platform_.WriteStringToFile(entry_path, "2"));

  uefi_delegate_->MakeEsrtReadableByFwupd(UefiDelegate::UserAndGroup{123, 456});
}

}  // namespace startup
