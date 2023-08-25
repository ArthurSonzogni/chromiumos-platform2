// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_unlock_template_command.h"

#include <algorithm>
#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::ElementsAreArray;

TEST(FpUnlockTemplateCommand, FpUnlockTemplateCommand) {
  constexpr uint16_t kFingers = 3;

  auto cmd = FpUnlockTemplateCommand::Create(kFingers);
  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_UNLOCK_TEMPLATE);
  EXPECT_EQ(cmd->Req()->fgr_num, kFingers);
}

}  // namespace
}  // namespace ec
