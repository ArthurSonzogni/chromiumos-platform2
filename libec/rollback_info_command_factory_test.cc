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

  auto cmd = RollbackInfoCommandFactory::Create(&mock_version_supported);

  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->GetVersion(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_ROLLBACK_INFO);
}

}  // namespace
}  // namespace ec
