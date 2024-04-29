// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensor_detail.h"

#include <map>
#include <utility>

#include <base/logging.h>
#include <base/values.h>
#include <iioservice/mojo/sensor.mojom.h>

#include "diagnostics/cros_healthd/routines/sensor/sensitive_sensor_constants.h"

namespace diagnostics {

namespace {

// Convert sensor device type enum to string.
std::string ConverDeviceTypeToString(cros::mojom::DeviceType type) {
  switch (type) {
    case cros::mojom::DeviceType::ACCEL:
      return kSensitiveSensorRoutineTypeAccel;
    case cros::mojom::DeviceType::ANGLVEL:
      return kSensitiveSensorRoutineTypeGyro;
    case cros::mojom::DeviceType::GRAVITY:
      return kSensitiveSensorRoutineTypeGravity;
    case cros::mojom::DeviceType::MAGN:
      return kSensitiveSensorRoutineTypeMagn;
    default:
      // The other sensor types are not supported in this routine.
      NOTREACHED_NORETURN();
  }
}

}  // namespace

void SensorDetail::UpdateChannelSample(int32_t indice, int64_t value) {
  // Passed channels are removed from |checking_channel_sample|.
  if (checking_channel_sample.find(indice) == checking_channel_sample.end())
    return;

  // First sample data for the channel.
  if (!checking_channel_sample[indice].has_value()) {
    checking_channel_sample[indice] = value;
    return;
  }

  // Remove channel when changed sample is found.
  if (value != checking_channel_sample[indice].value()) {
    checking_channel_sample.erase(indice);
  }
}

bool SensorDetail::IsErrorOccurred() {
  // Error getting channels.
  if (!channels.has_value()) {
    LOG(ERROR) << "Failed to get sensor channels.";
    return true;
  }

  // Error reading samples.
  for (const auto& [_, last_reading_sample] : checking_channel_sample) {
    if (!last_reading_sample.has_value()) {
      LOG(ERROR) << "Failed to read sensor sample.";
      return true;
    }
  }

  return false;
}

base::Value::Dict SensorDetail::ToDict() {
  base::Value::Dict sensor_output;
  sensor_output.Set("id", sensor_id);
  base::Value::List out_types;
  for (const auto& type : types)
    out_types.Append(ConverDeviceTypeToString(type));
  sensor_output.Set("types", std::move(out_types));
  base::Value::List out_channels;
  if (channels.has_value())
    for (const auto& channel_name : channels.value())
      out_channels.Append(channel_name);
  sensor_output.Set("channels", std::move(out_channels));
  return sensor_output;
}

}  // namespace diagnostics
