// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/values.h>
#include <iioservice/mojo/sensor.mojom-forward.h>

namespace diagnostics {

// The detail of sensor used for sensitive sensor routine. This is also a helper
// class to record read sensor sample.
class SensorDetail {
 public:
  // Return null if `types` don't contain any supported sensor types.
  static std::unique_ptr<SensorDetail> Create(
      int32_t sensor_id, const std::vector<cros::mojom::DeviceType>& types);
  SensorDetail(const SensorDetail&) = delete;
  SensorDetail& operator=(const SensorDetail&) = delete;
  ~SensorDetail();

  // Check the required sensor channels and update `checking_channel_sample_`
  // and `channels_`. Return indices of required channels for all sensor types
  // listed in `types_`. Return null if `sensor_channels` don't contain all
  // required channels.
  std::optional<std::vector<int32_t>> CheckRequiredChannelsAndGetIndices(
      const std::vector<std::string>& sensor_channels);

  // Update the read sample in `checking_channel_sample_` for channel at index
  // |indice|. Remove the channel from `checking_channel_sample_` when we
  // observe changed value.
  void UpdateChannelSample(int32_t indice, int64_t value);

  // Return true if we finish checking on all channels.
  bool AllChannelsChecked() const;

  // Check if there is any error when interacting with Iioservice.
  bool IsErrorOccurred() const;

  // Return the detail for v1 routine output dict.
  base::Value::Dict ToDict() const;

 protected:
  explicit SensorDetail(int32_t sensor_id,
                        const std::vector<cros::mojom::DeviceType>& types);

 private:
  // Sensor ID.
  const int32_t sensor_id_;

  // Sensor types.
  const std::vector<cros::mojom::DeviceType> types_;

  // Sensor channels.
  std::optional<std::vector<std::string>> channels_;

  // First is channel indice, second is the last reading sample. If the
  // channel finishes checking, remove it from this map.
  std::map<int32_t, std::optional<int64_t>> checking_channel_sample_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_DETAIL_H_
