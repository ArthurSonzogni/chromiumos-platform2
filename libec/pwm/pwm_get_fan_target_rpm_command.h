// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_PWM_PWM_GET_FAN_TARGET_RPM_COMMAND_H_
#define LIBEC_PWM_PWM_GET_FAN_TARGET_RPM_COMMAND_H_

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"
#include "libec/read_memmap_command.h"

namespace ec {

class BRILLO_EXPORT PwmGetFanTargetRpmCommand : public ReadMemmapMem16Command {
 public:
  explicit PwmGetFanTargetRpmCommand(uint8_t fan_idx)
      : ReadMemmapMem16Command(EC_MEMMAP_FAN + 2 * fan_idx) {
    SetRespSize(Req()->size);
  }
  ~PwmGetFanTargetRpmCommand() override = default;

  std::optional<uint16_t> Rpm() const {
    if (!Resp())
      return std::nullopt;
    return *Resp();
  }
};

static_assert(!std::is_copy_constructible<PwmGetFanTargetRpmCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<PwmGetFanTargetRpmCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_PWM_PWM_GET_FAN_TARGET_RPM_COMMAND_H_
