// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_sensor_device.h"

#include <utility>

namespace diagnostics {

FakeSensorDevice::FakeSensorDevice(const std::optional<std::string>& name,
                                   const std::optional<std::string>& location)
    : sensor_name_(name), sensor_location_(location) {}

void FakeSensorDevice::SetTimeout(uint32_t timeout) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::FakeSensorDevice::GetAttributes(
    const std::vector<std::string>& attr_names,
    GetAttributesCallback callback) {
  CHECK_EQ(attr_names.size(), 2);
  CHECK_EQ(attr_names[0], cros::mojom::kDeviceName);
  CHECK_EQ(attr_names[1], cros::mojom::kLocation);
  std::move(callback).Run({sensor_name_, sensor_location_});
}

void FakeSensorDevice::SetFrequency(double frequency,
                                    SetFrequencyCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::StartReadingSamples(
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::StopReadingSamples() {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::GetAllChannelIds(GetAllChannelIdsCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::SetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    SetChannelsEnabledCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::GetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    GetChannelsEnabledCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::GetChannelsAttributes(
    const std::vector<int32_t>& iio_chn_indices,
    const std::string& attr_name,
    GetChannelsAttributesCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::GetAllEvents(GetAllEventsCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::SetEventsEnabled(
    const std::vector<int32_t>& iio_event_indices,
    bool en,
    SetEventsEnabledCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::GetEventsEnabled(
    const std::vector<int32_t>& iio_event_indices,
    GetEventsEnabledCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::GetEventsAttributes(
    const std::vector<int32_t>& iio_event_indices,
    const std::string& attr_name,
    GetEventsAttributesCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::StartReadingEvents(
    mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> observer) {
  NOTIMPLEMENTED();
}

void FakeSensorDevice::StopReadingEvents() {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
