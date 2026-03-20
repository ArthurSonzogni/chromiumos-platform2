// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/rollback_info_command_factory.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/mock_ec_command_version_supported.h"

namespace ec {
namespace {

using ::testing::_;
using ::testing::Return;

TEST(RollbackInfoCommandFactoryTest, Create_v0) {
  MockEcCommandVersionSupported mock_version_supported;

  EXPECT_CALL(mock_version_supported,
              EcCmdVersionSupported(EC_CMD_ROLLBACK_INFO, 1))
      .WillOnce(Return(EcCmdVersionSupportStatus::UNSUPPORTED));

  auto cmd = RollbackInfoCommandFactory::Create(&mock_version_supported);

  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->GetVersion(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_ROLLBACK_INFO);
}

TEST(RollbackInfoCommandFactoryTest, Create_v1) {
  MockEcCommandVersionSupported mock_version_supported;

  EXPECT_CALL(mock_version_supported,
              EcCmdVersionSupported(EC_CMD_ROLLBACK_INFO, 1))
      .WillOnce(Return(EcCmdVersionSupportStatus::SUPPORTED));

  auto cmd = RollbackInfoCommandFactory::Create(&mock_version_supported);

  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 1);
  EXPECT_EQ(cmd->GetVersion(), 1);
  EXPECT_EQ(cmd->Command(), EC_CMD_ROLLBACK_INFO);
}

TEST(RollbackInfoCommandFactoryTest, Create_Version_Supported_Unknown) {
  MockEcCommandVersionSupported mock_version_supported;

  EXPECT_CALL(mock_version_supported,
              EcCmdVersionSupported(EC_CMD_ROLLBACK_INFO, 1))
      .WillOnce(Return(EcCmdVersionSupportStatus::UNKNOWN));

  auto cmd = RollbackInfoCommandFactory::Create(&mock_version_supported);

  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->GetVersion(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_ROLLBACK_INFO);
}

}  // namespace
}  // namespace ec
