// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/charge_state_command.h"

namespace ec {

ChargeStateCommand::ChargeStateCommand() : EcCommand(EC_CMD_CHARGE_STATE) {}

ChargeStateGetParamCommand::ChargeStateGetParamCommand(
    enum charge_state_params param) {
  Req()->cmd = CHARGE_STATE_CMD_GET_PARAM;
  Req()->get_param.param = param;
}

std::optional<uint32_t> ChargeStateGetParamCommand::Get() const {
  if (!Resp()) {
    return std::nullopt;
  }
  return Resp()->get_param.value;
}

}  // namespace ec
