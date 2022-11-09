// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo_service_manager/lib/connect.h>

#include "base/check.h"
#include "base/logging.h"
#include "mojo/service_constants.h"
#include "rmad/utils/mojo_service_utils.h"

namespace rmad {

void MojoServiceUtilsImpl::Initialize() {
  // Connect to the Mojo Service Manager.
  auto pending_remote =
      chromeos::mojo_service_manager::ConnectToMojoServiceManager();

  CHECK(pending_remote);
  service_manager_.Bind(std::move(pending_remote));

  // Bind the Sensor Service.
  service_manager_->Request(
      chromeos::mojo_services::kIioSensor, std::nullopt,
      sensor_service_.BindNewPipeAndPassReceiver().PassPipe());

  is_initialized = true;
}

cros::mojom::SensorDevice* MojoServiceUtilsImpl::GetSensorDevice(
    int device_id) {
  if (!is_initialized) {
    LOG(ERROR) << "The service is not yet initialized.";
    return nullptr;
  }

  // Bind the Sensor Device if it's not bound yet.
  if (sensor_devices_map_.find(device_id) == sensor_devices_map_.end()) {
    sensor_service_->GetDevice(
        device_id, sensor_devices_map_[device_id].BindNewPipeAndPassReceiver());
  }

  return sensor_devices_map_[device_id].get();
}

void MojoServiceUtilsImpl::SetSensorServiceForTesting(
    mojo::PendingRemote<cros::mojom::SensorService> service) {
  sensor_service_.Bind(std::move(service));
}

void MojoServiceUtilsImpl::SetInitializedForTesting() {
  is_initialized = true;
}

void MojoServiceUtilsImpl::InsertDeviceForTesting(int device_id) {
  sensor_service_->GetDevice(
      device_id, sensor_devices_map_[device_id].BindNewPipeAndPassReceiver());
}

}  // namespace rmad
