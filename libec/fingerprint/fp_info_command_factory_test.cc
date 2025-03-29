// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_command_factory.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/mock_ec_command_version_supported.h"

using testing::Return;

namespace ec {
namespace {

TEST(FpInfoCommandFactory, Create_v2) {
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported,
              EcCmdVersionSupported(EC_CMD_FP_INFO, 2))
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::SUPPORTED));

  auto cmd = FpInfoCommandFactory::Create(&mock_ec_cmd_ver_supported);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 2);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_INFO);
}

TEST(FpInfoCommandFactory, Create_v1) {
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported,
              EcCmdVersionSupported(EC_CMD_FP_INFO, 2))
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::UNSUPPORTED));

  auto cmd = FpInfoCommandFactory::Create(&mock_ec_cmd_ver_supported);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_INFO);
}

TEST(FpInfoCommandFactory, Create_Version_Supported_Unknown) {
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported,
              EcCmdVersionSupported(EC_CMD_FP_INFO, 2))
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::UNKNOWN));

  auto cmd = FpInfoCommandFactory::Create(&mock_ec_cmd_ver_supported);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_INFO);
}

}  // namespace
}  // namespace ec
