// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_PWM_PWM_SET_FAN_TARGET_RPM_COMMAND_H_
#define LIBEC_PWM_PWM_SET_FAN_TARGET_RPM_COMMAND_H_

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT PwmSetFanTargetRpmCommand
    : public EcCommand<struct ec_params_pwm_set_fan_target_rpm_v1, EmptyParam> {
 public:
  explicit PwmSetFanTargetRpmCommand(uint32_t rpm, uint8_t fan_idx)
      : EcCommand(EC_CMD_PWM_SET_FAN_TARGET_RPM, 1) {
    Req()->rpm = rpm;
    Req()->fan_idx = fan_idx;
  }
  ~PwmSetFanTargetRpmCommand() override = default;
};

static_assert(!std::is_copy_constructible<PwmSetFanTargetRpmCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<PwmSetFanTargetRpmCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_PWM_PWM_SET_FAN_TARGET_RPM_COMMAND_H_
