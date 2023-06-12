// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <gtest/gtest.h>

#include "libec/pwm/pwm_set_fan_target_rpm_command.h"

namespace ec {
namespace {

TEST(PwmSetFanTargetRpmCommand, PwmSetFanTargetRpmCommand) {
  constexpr uint32_t rpm = 3000;
  constexpr uint8_t fan_idx = 1;

  PwmSetFanTargetRpmCommand cmd(rpm, fan_idx);
  EXPECT_EQ(cmd.Command(), EC_CMD_PWM_SET_FAN_TARGET_RPM);
  EXPECT_GE(cmd.Version(), 1);
  EXPECT_EQ(cmd.Req()->rpm, rpm);
  EXPECT_EQ(cmd.Req()->fan_idx, fan_idx);
}

}  // namespace
}  // namespace ec
