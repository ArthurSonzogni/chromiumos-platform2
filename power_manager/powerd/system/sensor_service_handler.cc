// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/sensor_service_handler.h"

#include <utility>

#include <base/bind.h>
#include <base/threading/thread_task_runner_handle.h>

namespace power_manager {
namespace system {

SensorServiceHandler::SensorServiceHandler() = default;

SensorServiceHandler::~SensorServiceHandler() {
  ResetSensorService();
  sensor_hal_client_.reset();
}

void SensorServiceHandler::SetUpChannel(
    mojo::PendingRemote<cros::mojom::SensorService> pending_remote) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (sensor_service_remote_.is_bound()) {
    LOG(ERROR) << "Ignoring the second Remote<SensorService>";
    return;
  }

  sensor_service_remote_.Bind(std::move(pending_remote));
  sensor_service_remote_.set_disconnect_handler(
      base::BindOnce(&SensorServiceHandler::OnSensorServiceDisconnect,
                     base::Unretained(this)));

  sensor_service_remote_->RegisterNewDevicesObserver(
      new_devices_observer_.BindNewPipeAndPassRemote());
  new_devices_observer_.set_disconnect_handler(
      base::BindOnce(&SensorServiceHandler::OnNewDevicesObserverDisconnect,
                     base::Unretained(this)));

  sensor_service_remote_->GetAllDeviceIds(base::BindOnce(
      &SensorServiceHandler::GetAllDeviceIdsCallback, base::Unretained(this)));

  for (auto& observer : observers_)
    observer.SensorServiceConnected();
}

void SensorServiceHandler::OnNewDeviceAdded(
    int32_t iio_device_id, const std::vector<cros::mojom::DeviceType>& types) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  iio_device_ids_types_.emplace(iio_device_id, types);

  for (auto& observer : observers_)
    observer.OnNewDeviceAdded(iio_device_id, types);
}

void SensorServiceHandler::BindSensorHalClient(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
    OnMojoDisconnectCallback on_mojo_disconnect_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!sensor_hal_client_.is_bound());

  sensor_hal_client_.Bind(std::move(pending_receiver));
  sensor_hal_client_.set_disconnect_handler(
      base::BindOnce(&SensorServiceHandler::OnSensorHalClientDisconnect,
                     base::Unretained(this)));

  on_mojo_disconnect_callback_ = std::move(on_mojo_disconnect_callback);
}

void SensorServiceHandler::AddObserver(SensorServiceHandlerObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  observers_.AddObserver(observer);

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&SensorServiceHandler::NotifyObserverWithCurrentDevices,
                     weak_factory_.GetWeakPtr(), observer));
}

void SensorServiceHandler::RemoveObserver(
    SensorServiceHandlerObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  observers_.RemoveObserver(observer);
}

void SensorServiceHandler::GetDevice(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> pending_receiver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(sensor_service_remote_.is_bound());

  sensor_service_remote_->GetDevice(iio_device_id, std::move(pending_receiver));
}

void SensorServiceHandler::OnSensorHalClientDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(on_mojo_disconnect_callback_);

  LOG(ERROR) << "SensorHalClient connection lost";

  ResetSensorService();
  sensor_hal_client_.reset();

  std::move(on_mojo_disconnect_callback_).Run();
}

void SensorServiceHandler::OnSensorServiceDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(ERROR) << "SensorService connection lost";

  ResetSensorService();
}

void SensorServiceHandler::OnNewDevicesObserverDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(ERROR)
      << "OnNewDevicesObserverDisconnect, resetting SensorService as "
         "IIO Service should be destructed and waiting for it to relaunch.";
  ResetSensorService();
}

void SensorServiceHandler::ResetSensorService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (sensor_service_remote_.is_bound()) {
    for (auto& observer : observers_)
      observer.SensorServiceDisconnected();
  }

  new_devices_observer_.reset();
  sensor_service_remote_.reset();

  iio_device_ids_types_.clear();
}

void SensorServiceHandler::GetAllDeviceIdsCallback(
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        iio_device_ids_types) {
  iio_device_ids_types_ = iio_device_ids_types;

  for (auto& observer : observers_)
    NotifyObserverWithCurrentDevices(&observer);
}

void SensorServiceHandler::NotifyObserverWithCurrentDevices(
    SensorServiceHandlerObserver* observer) {
  for (auto& id_types : iio_device_ids_types_)
    observer->OnNewDeviceAdded(id_types.first, id_types.second);
}

}  // namespace system
}  // namespace power_manager
