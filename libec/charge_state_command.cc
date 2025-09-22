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

GetMinChargingVoltCommand::GetMinChargingVoltCommand()
    : ChargeStateGetParamCommand(CS_PARAM_CHG_MIN_REQUIRED_MV) {}

std::optional<double> GetMinChargingVoltCommand::Get() const {
  std::optional<uint32_t> mv = ChargeStateGetParamCommand::Get();
  if (!mv.has_value()) {
    return std::nullopt;
  }
  return static_cast<double>(*mv) / 1000.0;
}

}  // namespace ec
