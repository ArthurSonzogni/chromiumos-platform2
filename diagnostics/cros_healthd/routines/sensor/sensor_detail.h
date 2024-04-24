// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <base/values.h>
#include <iioservice/mojo/sensor.mojom-forward.h>

namespace diagnostics {

// The detail of sensor used for sensitive sensor routine.
struct SensorDetail {
  // Sensor types.
  std::vector<cros::mojom::DeviceType> types;
  // Sensor channels.
  std::optional<std::vector<std::string>> channels;
  // First is channel indice, second is the last reading sample. If the
  // channel finishes checking, remove it from this map.
  std::map<int32_t, std::optional<int64_t>> checking_channel_sample;

  // Update the sample for channel at index |indice|.
  void UpdateChannelSample(int32_t indice, int64_t value);

  // Check if there is any error when interacting with Iioservice.
  bool IsErrorOccurred();

  // Return the detail for output dict.
  base::Value::Dict GetDetailValue(int32_t id);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_
