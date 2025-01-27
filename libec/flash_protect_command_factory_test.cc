// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/flash_protect_command_factory.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/mock_ec_command_version_supported.h"

using testing::Return;

namespace ec {
namespace {

TEST(FlashProtectCommandFactory, Create_v2) {
  flash_protect::Flags flags =
      flash_protect::Flags::kRollbackAtBoot | flash_protect::Flags::kRoAtBoot;
  flash_protect::Flags mask = flash_protect::Flags::kNone;
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported, EcCmdVersionSupported)
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::SUPPORTED));

  auto cmd = FlashProtectCommandFactory::Create(&mock_ec_cmd_ver_supported,
                                                flags, mask);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 2);
}

TEST(FlashProtectCommandFactory, Create_v1) {
  flash_protect::Flags flags =
      flash_protect::Flags::kRollbackAtBoot | flash_protect::Flags::kRoAtBoot;
  flash_protect::Flags mask = flash_protect::Flags::kNone;
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported, EcCmdVersionSupported)
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::UNSUPPORTED));

  auto cmd = FlashProtectCommandFactory::Create(&mock_ec_cmd_ver_supported,
                                                flags, mask);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
}

TEST(FlashProtectCommandFactory, Create_Version_Supported_Unknown) {
  flash_protect::Flags flags =
      flash_protect::Flags::kRollbackAtBoot | flash_protect::Flags::kRoAtBoot;
  flash_protect::Flags mask = flash_protect::Flags::kNone;
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported, EcCmdVersionSupported)
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::UNKNOWN));

  auto cmd = FlashProtectCommandFactory::Create(&mock_ec_cmd_ver_supported,
                                                flags, mask);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
}

}  // namespace
}  // namespace ec
