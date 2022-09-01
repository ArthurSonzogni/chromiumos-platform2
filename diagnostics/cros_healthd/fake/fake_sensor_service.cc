// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_sensor_service.h"

#include <utility>

namespace diagnostics {

void FakeSensorService::SetIdsTypes(
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        ids_types) {
  ids_types_ = ids_types;
}

void FakeSensorService::GetDeviceIds(cros::mojom::DeviceType type,
                                     GetDeviceIdsCallback callback) {
  NOTIMPLEMENTED();
}

void FakeSensorService::GetAllDeviceIds(GetAllDeviceIdsCallback callback) {
  std::move(callback).Run(ids_types_);
}

void FakeSensorService::GetDevice(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> device_request) {
  NOTIMPLEMENTED();
}

void FakeSensorService::RegisterNewDevicesObserver(
    mojo::PendingRemote<cros::mojom::SensorServiceNewDevicesObserver>
        observer) {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
