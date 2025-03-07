// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_watcher_mojo.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <base/containers/contains.h>

namespace power_manager::system {

AmbientLightSensorWatcherMojo::AmbientLightSensorWatcherMojo(
    SensorServiceHandler* sensor_service_handler)
    : SensorServiceHandlerObserver(sensor_service_handler) {}

void AmbientLightSensorWatcherMojo::OnNewDeviceAdded(
    int32_t iio_device_id, const std::vector<cros::mojom::DeviceType>& types) {
  if (std::find(types.begin(), types.end(), cros::mojom::DeviceType::LIGHT) ==
      types.end()) {
    // Not a light sensor. Ignoring this device.
    return;
  }

  for (const AmbientLightSensorInfo& als : ambient_light_sensors_) {
    if (als.id == iio_device_id) {
      // Has already added this device.
      return;
    }
  }

  if (base::Contains(device_remotes_, iio_device_id)) {
    // Has already added this device.
    return;
  }

  auto remote = GetDevice(iio_device_id);

  remote->GetAttributes(
      std::vector<std::string>{cros::mojom::kSysPath},
      base::BindOnce(&AmbientLightSensorWatcherMojo::GetSysPathCallback,
                     base::Unretained(this), iio_device_id));

  device_remotes_.emplace(iio_device_id, std::move(remote));
}

void AmbientLightSensorWatcherMojo::OnDeviceRemoved(int32_t iio_device_id) {
  LOG(WARNING) << "OnDeviceRemoved: " << iio_device_id;

  device_remotes_.erase(iio_device_id);
  for (auto itr = ambient_light_sensors_.begin();
       itr != ambient_light_sensors_.end(); itr++) {
    if (itr->id == iio_device_id) {
      ambient_light_sensors_.erase(itr);
      NotifyObservers();
      break;
    }
  }
}

void AmbientLightSensorWatcherMojo::SensorServiceConnected() {
  // Nothing to do.
}

void AmbientLightSensorWatcherMojo::SensorServiceDisconnected() {
  device_remotes_.clear();
  ambient_light_sensors_.clear();
  NotifyObservers();
}

mojo::Remote<cros::mojom::SensorDevice>
AmbientLightSensorWatcherMojo::GetDevice(int32_t iio_device_id) {
  DCHECK_GE(iio_device_id, 0);

  mojo::Remote<cros::mojom::SensorDevice> remote;
  sensor_service_handler_->GetDevice(iio_device_id,
                                     remote.BindNewPipeAndPassReceiver());

  return remote;
}

void AmbientLightSensorWatcherMojo::GetSysPathCallback(
    int32_t iio_device_id,
    const std::vector<std::optional<std::string>>& values) {
  auto it = device_remotes_.find(iio_device_id);
  DCHECK(it != device_remotes_.end());

  // No longer need to contain the remote, while still keep the id entry.
  it->second.reset();

  if (values.empty() || !values[0].has_value()) {
    LOG(ERROR) << "Sensor values doesn't contain the syspath attribute.";
    return;
  }

  if (values.size() != 1) {
    LOG(WARNING)
        << "Sensor values contain more than the syspath attribute. Size: "
        << values.size();
  }

  AmbientLightSensorInfo new_als = {
      .iio_path = base::FilePath(values[0].value()),
      .id = iio_device_id,
  };

  AddSensorAndNotifyObservers(std::move(new_als));
}

}  // namespace power_manager::system
