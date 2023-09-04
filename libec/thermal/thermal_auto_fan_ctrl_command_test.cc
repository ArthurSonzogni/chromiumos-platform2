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
  constexpr uint8_t fan_idx = 1;

  ThermalAutoFanCtrlCommand cmd{fan_idx};
  EXPECT_EQ(cmd.Command(), EC_CMD_THERMAL_AUTO_FAN_CTRL);
  EXPECT_GE(cmd.Version(), 1);
  EXPECT_EQ(cmd.Req()->fan_idx, fan_idx);
}

}  // namespace
}  // namespace ec
