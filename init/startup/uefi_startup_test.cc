// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "init/startup/constants.h"
#include "init/startup/mock_platform_impl.h"
#include "init/startup/uefi_startup.h"
#include "init/startup/uefi_startup_impl.h"

using testing::Return;
using testing::StrictMock;

namespace startup {

class MockUefiDelegate : public UefiDelegate {
 public:
  MockUefiDelegate() = default;

  MOCK_METHOD(bool, IsUefiEnabled, (), (const, override));
  MOCK_METHOD(bool, MountEfivarfs, (), (override));
};

// Test that the appropriate actions are taken if UEFI is enabled.
TEST(UefiStartup, UefiEnabled) {
  StrictMock<MockUefiDelegate> mock_uefi_delegate;

  EXPECT_CALL(mock_uefi_delegate, IsUefiEnabled()).WillOnce(Return(true));
  EXPECT_CALL(mock_uefi_delegate, MountEfivarfs()).WillOnce(Return(true));

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
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_dir_ = temp_dir_.GetPath();

    uefi_delegate_ =
        std::make_unique<UefiDelegateImpl>(mock_platform_, root_dir_);
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath root_dir_;

  StrictMock<MockPlatform> mock_platform_;
  std::unique_ptr<UefiDelegate> uefi_delegate_;
};

// Test `IsUefiEnabled` when UEFI is enabled.
TEST_F(UefiDelegateTest, IsUefiEnabledYes) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  ASSERT_TRUE(base::CreateDirectory(efivars_dir));

  EXPECT_TRUE(uefi_delegate_->IsUefiEnabled());
}

// Test `IsUefiEnabled` when UEFI is not enabled.
TEST_F(UefiDelegateTest, IsUefiEnabledNo) {
  const base::FilePath efivars_dir = root_dir_.Append("sys/firmware");
  ASSERT_TRUE(base::CreateDirectory(efivars_dir));

  EXPECT_FALSE(uefi_delegate_->IsUefiEnabled());
}

// Test mounting efivarfs.
TEST_F(UefiDelegateTest, MountEfivarfs) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  ASSERT_TRUE(base::CreateDirectory(efivars_dir));

  EXPECT_CALL(mock_platform_, Mount(kFsTypeEfivarfs, efivars_dir,
                                    kFsTypeEfivarfs, kCommonMountFlags, ""))
      .WillOnce(Return(true));

  EXPECT_TRUE(uefi_delegate_->MountEfivarfs());
}

}  // namespace startup
