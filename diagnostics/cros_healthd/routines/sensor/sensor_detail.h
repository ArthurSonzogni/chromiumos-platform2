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

// The detail of sensor used for sensitive sensor routine. This is also a helper
// class to record read sensor sample.
struct SensorDetail {
  // Sensor ID.
  int32_t sensor_id;

  // Sensor types.
  std::vector<cros::mojom::DeviceType> types;

  // Sensor channels.
  std::optional<std::vector<std::string>> channels;

  // First is channel indice, second is the last reading sample. If the
  // channel finishes checking, remove it from this map.
  std::map<int32_t, std::optional<int64_t>> checking_channel_sample;

  // Check the required sensor channels and update `checking_channel_sample` and
  // `channels`. Return indices of required channels for all sensor types listed
  // in `types`. Return null if `sensor_channels` don't contain all required
  // channels.
  std::optional<std::vector<int32_t>> CheckRequiredChannelsAndGetIndices(
      const std::vector<std::string>& sensor_channels);

  // Update the read sample in `checking_channel_sample` for channel at index
  // |indice|. Remove the channel from `checking_channel_sample` when we
  // observe changed value.
  void UpdateChannelSample(int32_t indice, int64_t value);

  // Return true if we finish checking on all channels.
  bool AllChannelsChecked();

  // Check if there is any error when interacting with Iioservice.
  bool IsErrorOccurred();

  // Return the detail for v1 routine output dict.
  base::Value::Dict ToDict();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_
