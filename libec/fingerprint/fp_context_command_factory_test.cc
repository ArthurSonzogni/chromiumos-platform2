// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_context_command_factory.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/mock_ec_command_version_supported.h"

using testing::Return;

namespace ec {
namespace {

TEST(FpContextCommandFactory, Create_v1) {
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported, EcCmdVersionSupported)
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::SUPPORTED));

  auto cmd =
      FpContextCommandFactory::Create(&mock_ec_cmd_ver_supported, "DEADBEEF");
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
}

TEST(FpContextCommandFactory, Create_v0) {
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported, EcCmdVersionSupported)
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::UNSUPPORTED));

  auto cmd =
      FpContextCommandFactory::Create(&mock_ec_cmd_ver_supported, "DEADBEEF");
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
}

TEST(FpContextCommandFactory, Create_Version_Supported_Unknown) {
  ec::MockEcCommandVersionSupported mock_ec_cmd_ver_supported;
  EXPECT_CALL(mock_ec_cmd_ver_supported, EcCmdVersionSupported)
      .Times(1)
      .WillOnce(Return(EcCmdVersionSupportStatus::UNKNOWN));

  auto cmd =
      FpContextCommandFactory::Create(&mock_ec_cmd_ver_supported, "DEADBEEF");
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
}

}  // namespace
}  // namespace ec
