// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "libec/set_force_lid_open_command.h"

namespace ec {
namespace {

TEST(SetForceLidOpenCommand, EnableForceLidOpen) {
  SetForceLidOpenCommand cmd(true);
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_FORCE_LID_OPEN);
  EXPECT_EQ(cmd.Req()->enabled, true);
}

TEST(SetForceLidOpenCommand, DisableForceLidOpen) {
  SetForceLidOpenCommand cmd(false);
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_FORCE_LID_OPEN);
  EXPECT_EQ(cmd.Req()->enabled, false);
}

}  // namespace
}  // namespace ec
