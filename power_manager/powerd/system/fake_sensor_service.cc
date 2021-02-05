// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/fake_sensor_service.h"

#include <utility>

namespace power_manager {
namespace system {

FakeSensorService::FakeSensorService() = default;
FakeSensorService::~FakeSensorService() = default;

void FakeSensorService::AddReceiver(
    mojo::PendingReceiver<cros::mojom::SensorService> pending_receiver) {
  receiver_set_.Add(this, std::move(pending_receiver));
}

void FakeSensorService::ClearReceivers() {
  receiver_set_.Clear();
}

bool FakeSensorService::HasReceivers() const {
  return !receiver_set_.empty();
}

void FakeSensorService::SetSensorDevice(
    int32_t iio_device_id, std::unique_ptr<FakeSensorDevice> sensor_device) {
  sensor_devices_[iio_device_id] = std::move(sensor_device);

  for (auto& observer : observers_) {
    observer->OnNewDeviceAdded(
        iio_device_id,
        std::vector<cros::mojom::DeviceType>{cros::mojom::DeviceType::LIGHT});
  }
}

void FakeSensorService::GetDeviceIds(cros::mojom::DeviceType type,
                                     GetDeviceIdsCallback callback) {
  std::vector<int32_t> ids;
  if (type == cros::mojom::DeviceType::LIGHT) {
    for (const auto& sensor_device : sensor_devices_)
      ids.push_back(sensor_device.first);
  }

  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(ids)));
}

void FakeSensorService::GetAllDeviceIds(GetAllDeviceIdsCallback callback) {
  base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>> id_types;
  for (const auto& sensor_device : sensor_devices_) {
    id_types.emplace(sensor_device.first, std::vector<cros::mojom::DeviceType>{
                                              cros::mojom::DeviceType::LIGHT});
  }

  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(id_types)));
}

void FakeSensorService::GetDevice(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> device_request) {
  auto it = sensor_devices_.find(iio_device_id);
  if (it == sensor_devices_.end())
    return;

  it->second->AddReceiver(std::move(device_request));
}

void FakeSensorService::RegisterNewDevicesObserver(
    mojo::PendingRemote<cros::mojom::SensorServiceNewDevicesObserver>
        observer) {
  observers_.emplace_back(
      mojo::Remote<cros::mojom::SensorServiceNewDevicesObserver>(
          std::move(observer)));
}

}  // namespace system
}  // namespace power_manager
