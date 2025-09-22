// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_CHARGE_STATE_COMMAND_H_
#define LIBEC_CHARGE_STATE_COMMAND_H_

#include <cstdint>
#include <optional>

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT ChargeStateCommand
    : public EcCommand<struct ec_params_charge_state,
                       struct ec_response_charge_state> {
 public:
  ~ChargeStateCommand() override = default;

 protected:
  ChargeStateCommand();
};

class BRILLO_EXPORT ChargeStateGetParamCommand : public ChargeStateCommand {
 public:
  explicit ChargeStateGetParamCommand(enum charge_state_params param);
  ~ChargeStateGetParamCommand() override = default;

  std::optional<uint32_t> Get() const;
};

class BRILLO_EXPORT GetMinChargingVoltCommand
    : public ChargeStateGetParamCommand {
 public:
  GetMinChargingVoltCommand();
  ~GetMinChargingVoltCommand() override = default;

  std::optional<double> Get() const;
};

static_assert(!std::is_copy_constructible<ChargeStateCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<ChargeStateCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_CHARGE_STATE_COMMAND_H_
