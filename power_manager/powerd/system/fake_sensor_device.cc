// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/fake_sensor_device.h"

#include <utility>

#include "power_manager/powerd/system/ambient_light_sensor_delegate_mojo.h"

#include <base/check.h>

namespace power_manager {
namespace system {

FakeSensorDevice::FakeSensorDevice(bool is_color_sensor,
                                   base::Optional<std::string> name,
                                   base::Optional<std::string> location)
    : is_color_sensor_(is_color_sensor) {
  if (name.has_value())
    attributes_[cros::mojom::kDeviceName] = name.value();

  if (location.has_value())
    attributes_[cros::mojom::kLocation] = location.value();
}

mojo::ReceiverId FakeSensorDevice::AddReceiver(
    mojo::PendingReceiver<cros::mojom::SensorDevice> pending_receiver) {
  return receiver_set_.Add(this, std::move(pending_receiver));
}

bool FakeSensorDevice::HasReceivers() const {
  return !receiver_set_.empty();
}

void FakeSensorDevice::ClearReceiverWithReason(
    cros::mojom::SensorDeviceDisconnectReason reason,
    const std::string& description) {
  uint32_t custom_reason_code = base::checked_cast<uint32_t>(reason);

  for (auto& observer : observers_) {
    auto remote = mojo::Remote<cros::mojom::SensorDeviceSamplesObserver>(
        std::move(observer.second));
    remote.ResetWithReason(custom_reason_code, description);
  }
  observers_.clear();

  receiver_set_.ClearWithReason(custom_reason_code, description);
}

void FakeSensorDevice::ResetObserverRemote(mojo::ReceiverId id) {
  auto it = observers_.find(id);
  DCHECK(it != observers_.end());

  observers_.erase(it);
}

void FakeSensorDevice::GetAttributes(const std::vector<std::string>& attr_names,
                                     GetAttributesCallback callback) {
  std::vector<base::Optional<std::string>> attr_values;
  attr_values.reserve(attr_names.size());
  for (const auto& attr_name : attr_names) {
    auto it = attributes_.find(attr_name);
    if (it != attributes_.end())
      attr_values.push_back(it->second);
    else
      attr_values.push_back(base::nullopt);
  }

  std::move(callback).Run(std::move(attr_values));
}

void FakeSensorDevice::SetFrequency(double frequency,
                                    SetFrequencyCallback callback) {
  std::move(callback).Run(frequency);
}

void FakeSensorDevice::StartReadingSamples(
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  observers_.emplace(receiver_set_.current_receiver(), std::move(observer));
}

void FakeSensorDevice::StopReadingSamples() {
  observers_.erase(receiver_set_.current_receiver());
}

void FakeSensorDevice::GetAllChannelIds(GetAllChannelIdsCallback callback) {
  std::vector<std::string> channel_ids(1, cros::mojom::kLightChannel);
  if (is_color_sensor_) {
    for (const ColorChannelInfo& channel : kColorChannelConfig) {
      channel_ids.push_back(
          AmbientLightSensorDelegateMojo::GetChannelIlluminanceColorId(
              channel.rgb_name));
    }
  }
  channel_ids.push_back(cros::mojom::kTimestampChannel);
  std::move(callback).Run(std::move(channel_ids));
}

void FakeSensorDevice::SetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    SetChannelsEnabledCallback callback) {
  std::move(callback).Run(std::move(std::vector<int32_t>{}));
}

void FakeSensorDevice::GetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    GetChannelsEnabledCallback callback) {
  std::move(callback).Run(
      std::move(std::vector<bool>(iio_chn_indices.size(), true)));
}

void FakeSensorDevice::GetChannelsAttributes(
    const std::vector<int32_t>& iio_chn_indices,
    const std::string& attr_name,
    GetChannelsAttributesCallback callback) {
  std::move(callback).Run(std::move(std::vector<base::Optional<std::string>>(
      iio_chn_indices.size(), base::nullopt)));
}

}  // namespace system
}  // namespace power_manager
