// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_MOJO_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_MOJO_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/sequence_checker.h>
#include <iioservice/mojo/cros_sensor_service.mojom.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/ambient_light_sensor.h"
#include "power_manager/powerd/system/ambient_light_sensor_delegate_mojo.h"
#include "power_manager/powerd/system/ambient_light_sensor_manager_interface.h"

namespace power_manager {

class PrefsInterface;

namespace system {

// AmbientLightSensorManagerMojo should be used on the same thread.
class AmbientLightSensorManagerMojo
    : public AmbientLightSensorManagerInterface,
      public cros::mojom::SensorHalClient,
      public cros::mojom::SensorServiceNewDevicesObserver {
 public:
  using OnMojoDisconnectCallback = base::OnceCallback<void()>;

  explicit AmbientLightSensorManagerMojo(PrefsInterface* prefs);
  AmbientLightSensorManagerMojo(const AmbientLightSensorManagerMojo&) = delete;
  AmbientLightSensorManagerMojo& operator=(
      const AmbientLightSensorManagerMojo&) = delete;
  ~AmbientLightSensorManagerMojo() override;

  // AmbientLightSensorManagerInterface overrides:
  AmbientLightSensorInterface* GetSensorForInternalBacklight() override;
  AmbientLightSensorInterface* GetSensorForKeyboardBacklight() override;
  bool HasColorSensor() override;

  // cros::mojom::SensorHalClient overrides:
  void SetUpChannel(
      mojo::PendingRemote<cros::mojom::SensorService> pending_remote) override;

  // cros::mojom::SensorServiceNewDevicesObserver overrides:
  void OnNewDeviceAdded(
      int32_t iio_device_id,
      const std::vector<cros::mojom::DeviceType>& types) override;

  void BindSensorHalClient(
      mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
      OnMojoDisconnectCallback on_mojo_disconnect_callback);

 private:
  struct Sensor {
    base::Optional<int> iio_device_id;
    system::AmbientLightSensor* sensor = nullptr;
  };

  struct LightData {
    // Something is wrong of the attributes, or this light sensor is not needed.
    bool ignored = false;

    base::Optional<std::string> name;
    base::Optional<SensorLocation> location;

    // Temporarily stores the accelerometer mojo::Remote, waiting for its
    // attribute information. It'll be passed to AmbientLightSensorDelegateMojo
    // as an argument after all information is collected.
    mojo::Remote<cros::mojom::SensorDevice> remote;
  };

  void OnSensorHalClientDisconnect();

  void OnSensorServiceDisconnect();
  void ResetSensorService();

  // Called when an in-use device is unplugged, and we need to search for other
  // devices to use.
  void ResetStates();
  void QueryDevices();

  void OnNewDevicesObserverDisconnect();
  void OnSensorDeviceDisconnect(int32_t id,
                                uint32_t custom_reason_code,
                                const std::string& description);

  // Gets device ids from IIO Service and chooses sensors among them.
  void GetDeviceIdsCallback(const std::vector<int32_t>& iio_device_ids);
  void GetNameCallback(int32_t id,
                       const std::vector<base::Optional<std::string>>& values);
  void GetNameAndLocationCallback(
      int32_t id, const std::vector<base::Optional<std::string>>& values);
  void SetSensorDeviceAtLocation(int32_t id, SensorLocation location);

  void AllDevicesFound();

  void SetSensorDeviceMojo(Sensor* sensor, bool allow_ambient_eq);

  int64_t num_sensors_ = 0;
  bool allow_ambient_eq_ = false;

  mojo::Receiver<cros::mojom::SensorHalClient> sensor_hal_client_{this};
  OnMojoDisconnectCallback on_mojo_disconnect_callback_;

  mojo::Remote<cros::mojom::SensorService> sensor_service_remote_;

  // The Mojo channel to get notified when new devices are added to IIO Service.
  mojo::Receiver<cros::mojom::SensorServiceNewDevicesObserver>
      new_devices_observer_{this};

  // First is the device id, second is it's data and mojo remote. Only used if
  // |num_sensors_| is greater or equals to 2.
  std::map<int32_t, LightData> lights_;

  std::vector<std::unique_ptr<AmbientLightSensor>> sensors_;

  // iio_device_ids and Weak pointers into the relevant entries of |sensors_|.
  Sensor lid_sensor_;
  Sensor base_sensor_;

  base::WeakPtrFactory<AmbientLightSensorManagerMojo> weak_factory_{this};

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_MOJO_H_
