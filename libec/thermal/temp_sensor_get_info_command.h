// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_THERMAL_TEMP_SENSOR_GET_INFO_COMMAND_H_
#define LIBEC_THERMAL_TEMP_SENSOR_GET_INFO_COMMAND_H_

#include <brillo/brillo_export.h>

#include <cstdint>
#include <string>

#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT TempSensorGetInfoCommand
    : public EcCommand<ec_params_temp_sensor_get_info,
                       ec_response_temp_sensor_get_info> {
 public:
  explicit TempSensorGetInfoCommand(uint8_t id)
      : EcCommand(EC_CMD_TEMP_SENSOR_GET_INFO, 0) {
    Req()->id = id;
  }
  ~TempSensorGetInfoCommand() override = default;
  std::optional<std::string> SensorName() const {
    if (!Resp())
      return std::nullopt;
    return Resp()->sensor_name;
  }
  std::optional<uint8_t> SensorType() const {
    if (!Resp())
      return std::nullopt;
    return Resp()->sensor_type;
  }
};

static_assert(!std::is_copy_constructible<TempSensorGetInfoCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<TempSensorGetInfoCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_THERMAL_TEMP_SENSOR_GET_INFO_COMMAND_H_
