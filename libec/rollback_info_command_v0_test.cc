// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/rollback_info_command.h"

namespace ec {
namespace {

using ::testing::Return;

TEST(RollbackInfoCommand_v0, RollbackInfoCommand_v0) {
  RollbackInfoCommand_v0 cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_ROLLBACK_INFO);
}

}  // namespace
}  // namespace ec
