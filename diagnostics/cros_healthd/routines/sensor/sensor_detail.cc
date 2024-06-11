// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensor_detail.h"

#include <map>
#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <iioservice/mojo/sensor.mojom.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kChannelAxes[] = {'x', 'y', 'z'};

// The sensitive sensor routine only supports accelerometers, magnetometers,
// gyro sensors, gravity sensors.
std::vector<cros::mojom::DeviceType> FilterSupportedTypes(
    const std::vector<cros::mojom::DeviceType>& types) {
  auto is_supported_type = [](cros::mojom::DeviceType type) {
    switch (type) {
      case cros::mojom::DeviceType::ACCEL:
      case cros::mojom::DeviceType::MAGN:
      case cros::mojom::DeviceType::ANGLVEL:
      case cros::mojom::DeviceType::GRAVITY:
        return true;
      default:
        return false;
    }
  };
  std::vector<cros::mojom::DeviceType> supported_types;
  std::copy_if(types.begin(), types.end(), std::back_inserter(supported_types),
               is_supported_type);
  return supported_types;
}

// Convert sensor device type enum to channel prefix.
std::string ConvertDeviceTypeToChannelPrefix(cros::mojom::DeviceType type) {
  switch (type) {
    case cros::mojom::DeviceType::ACCEL:
      return cros::mojom::kAccelerometerChannel;
    case cros::mojom::DeviceType::ANGLVEL:
      return cros::mojom::kGyroscopeChannel;
    case cros::mojom::DeviceType::GRAVITY:
      return cros::mojom::kGravityChannel;
    case cros::mojom::DeviceType::MAGN:
      return cros::mojom::kMagnetometerChannel;
    default:
      // The other sensor types are not supported in this routine.
      NOTREACHED_NORETURN();
  }
}

// Convert sensor device type enum to routines mojom enum.
mojom::SensitiveSensorInfo::Type Convert(cros::mojom::DeviceType type) {
  switch (type) {
    case cros::mojom::DeviceType::ACCEL:
      return mojom::SensitiveSensorInfo::Type::kAccel;
    case cros::mojom::DeviceType::ANGLVEL:
      return mojom::SensitiveSensorInfo::Type::kGyro;
    case cros::mojom::DeviceType::MAGN:
      return mojom::SensitiveSensorInfo::Type::kMagn;
    case cros::mojom::DeviceType::GRAVITY:
      return mojom::SensitiveSensorInfo::Type::kGravity;
    default:
      // The other sensor types are not supported in this routine.
      NOTREACHED_NORETURN();
  }
}

// Get required channels for sensor types listed in `types`.
std::vector<std::string> GetRequiredChannels(
    const std::vector<cros::mojom::DeviceType>& types) {
  std::vector<std::string> channels = {cros::mojom::kTimestampChannel};
  for (auto type : types) {
    auto channel_prefix = ConvertDeviceTypeToChannelPrefix(type);
    for (char axis : kChannelAxes)
      channels.push_back(channel_prefix + "_" + axis);
  }
  return channels;
}

}  // namespace

std::unique_ptr<SensorDetail> SensorDetail::Create(
    int32_t sensor_id, const std::vector<cros::mojom::DeviceType>& types) {
  auto supported_types = FilterSupportedTypes(types);
  if (supported_types.empty()) {
    return nullptr;
  }
  return base::WrapUnique(new SensorDetail(sensor_id, supported_types));
}

SensorDetail::SensorDetail(int32_t sensor_id,
                           const std::vector<cros::mojom::DeviceType>& types)
    : sensor_id_(sensor_id), types_{types} {}

SensorDetail::~SensorDetail() = default;

std::optional<std::vector<int32_t>>
SensorDetail::CheckRequiredChannelsAndGetIndices(
    const std::vector<std::string>& sensor_channels) {
  channels_ = sensor_channels;

  std::vector<int32_t> channel_indices;
  for (auto required_channel : GetRequiredChannels(types_)) {
    auto it = std::find(sensor_channels.begin(), sensor_channels.end(),
                        required_channel);
    if (it == sensor_channels.end()) {
      return std::nullopt;
    }
    int32_t indice = it - sensor_channels.begin();
    channel_indices.push_back(indice);
    // Set the indeice of required channel to check samples.
    checking_channel_sample_[indice] = std::nullopt;
  }

  return channel_indices;
}

void SensorDetail::UpdateChannelSample(int32_t indice, int64_t value) {
  // Passed channels are removed from |checking_channel_sample_|.
  if (checking_channel_sample_.find(indice) == checking_channel_sample_.end())
    return;

  // First sample data for the channel.
  if (!checking_channel_sample_[indice].has_value()) {
    checking_channel_sample_[indice] = value;
    return;
  }

  // Remove channel when changed sample is found.
  if (value != checking_channel_sample_[indice].value()) {
    checking_channel_sample_.erase(indice);
  }
}

bool SensorDetail::AllChannelsChecked() const {
  return checking_channel_sample_.empty();
}

bool SensorDetail::IsErrorOccurred() const {
  // Error getting channels.
  if (!channels_.has_value()) {
    LOG(ERROR) << "Failed to get sensor channels.";
    return true;
  }

  // Error reading samples.
  for (const auto& [_, last_reading_sample] : checking_channel_sample_) {
    if (!last_reading_sample.has_value()) {
      LOG(ERROR) << "Failed to read sensor sample.";
      return true;
    }
  }

  return false;
}

mojom::SensitiveSensorInfoPtr SensorDetail::ToMojo() const {
  auto output = mojom::SensitiveSensorInfo::New();
  output->id = sensor_id_;
  for (const auto& type : types_) {
    output->types.push_back(Convert(type));
  }
  if (channels_.has_value()) {
    output->channels = channels_.value();
  }
  return output;
}

}  // namespace diagnostics
