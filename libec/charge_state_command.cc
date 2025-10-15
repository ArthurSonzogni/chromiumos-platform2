// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/charge_state_command.h"

#include <cstring>

namespace ec {

ChargeStateCommand::ChargeStateCommand() : EcCommand(EC_CMD_CHARGE_STATE) {}

ChargeStateGetParamCommand::ChargeStateGetParamCommand(
    enum charge_state_params param) {
  Req()->cmd = CHARGE_STATE_CMD_GET_PARAM;
  SetParam(param);
  SetRespSize(sizeof(ec_response_charge_state::get_param));
}

std::optional<uint32_t> ChargeStateGetParamCommand::Get() const {
  if (!Resp()) {
    return std::nullopt;
  }
  // Direct access is safe because ec_response_charge_state is __aligned(4)__.
  return Resp()->get_param.value;
}

enum charge_state_params ChargeStateGetParamCommand::GetParam() const {
  // To avoid a misaligned access error from ASAN, we must copy the 4 bytes of
  // data from the packed request struct into a local, aligned variable.
  enum charge_state_params param;
  static_assert(sizeof(Req()->get_param.param) == sizeof(param),
                "Size of param and source do not match");
  std::memcpy(&param, &Req()->get_param.param, sizeof(param));
  return param;
}

void ChargeStateGetParamCommand::SetParam(enum charge_state_params param) {
  // Use std::memcpy to safely handle potential misalignment of the 'param'
  // member in the packed request struct. This copies the 4 bytes of the 'param'
  // enum into the request structure without an unaligned access.
  static_assert(sizeof(Req()->get_param.param) == sizeof(param),
                "Size of param and destination do not match");
  std::memcpy(&Req()->get_param.param, &param, sizeof(param));
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
