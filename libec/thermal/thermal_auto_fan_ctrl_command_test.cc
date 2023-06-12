// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>

#include "libec/thermal/thermal_auto_fan_ctrl_command.h"

namespace ec {
namespace {

TEST(ThermalAutoFanCtrlCommandTest, ThermalAutoFanCtrlCommand) {
  ThermalAutoFanCtrlCommand cmd;
  EXPECT_EQ(cmd.Command(), EC_CMD_THERMAL_AUTO_FAN_CTRL);
  EXPECT_GE(cmd.Version(), 0);
}

}  // namespace
}  // namespace ec
