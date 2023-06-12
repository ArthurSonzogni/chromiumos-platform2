// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_THERMAL_THERMAL_AUTO_FAN_CTRL_COMMAND_H_
#define LIBEC_THERMAL_THERMAL_AUTO_FAN_CTRL_COMMAND_H_

#include <brillo/brillo_export.h>

#include <cstdint>
#include <string>

#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT ThermalAutoFanCtrlCommand
    : public EcCommand<EmptyParam, EmptyParam> {
 public:
  ThermalAutoFanCtrlCommand() : EcCommand(EC_CMD_THERMAL_AUTO_FAN_CTRL, 0) {}
  ~ThermalAutoFanCtrlCommand() override = default;
};

static_assert(!std::is_copy_constructible<ThermalAutoFanCtrlCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<ThermalAutoFanCtrlCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_THERMAL_THERMAL_AUTO_FAN_CTRL_COMMAND_H_
