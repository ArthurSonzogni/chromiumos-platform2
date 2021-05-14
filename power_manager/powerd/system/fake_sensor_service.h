// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_FAKE_SENSOR_SERVICE_H_
#define POWER_MANAGER_POWERD_SYSTEM_FAKE_SENSOR_SERVICE_H_

#include <map>
#include <memory>
#include <vector>

#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "power_manager/powerd/system/fake_sensor_device.h"

namespace power_manager {
namespace system {

class FakeSensorService : public cros::mojom::SensorService {
 public:
  FakeSensorService();
  ~FakeSensorService() override;

  void AddReceiver(
      mojo::PendingReceiver<cros::mojom::SensorService> pending_receiver);

  void ClearReceivers();
  bool HasReceivers() const;

  void SetSensorDevice(int32_t iio_device_id,
                       std::unique_ptr<FakeSensorDevice> fake_sensor_device);

  // Implementation of cros::mojom::SensorService.
  void GetDeviceIds(cros::mojom::DeviceType type,
                    GetDeviceIdsCallback callback) override;
  void GetAllDeviceIds(GetAllDeviceIdsCallback callback) override;
  void GetDevice(
      int32_t iio_device_id,
      mojo::PendingReceiver<cros::mojom::SensorDevice> device_request) override;
  void RegisterNewDevicesObserver(
      mojo::PendingRemote<cros::mojom::SensorServiceNewDevicesObserver>
          observer) override;

  std::map<int32_t, std::unique_ptr<FakeSensorDevice>> sensor_devices_;

  mojo::ReceiverSet<cros::mojom::SensorService> receiver_set_;
  std::vector<mojo::Remote<cros::mojom::SensorServiceNewDevicesObserver>>
      observers_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_FAKE_SENSOR_SERVICE_H_
