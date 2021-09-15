// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_manager_mojo.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"

namespace power_manager {
namespace system {

AmbientLightSensorInterface*
AmbientLightSensorManagerMojo::GetSensorForInternalBacklight() {
  return lid_sensor_.sensor;
}

AmbientLightSensorInterface*
AmbientLightSensorManagerMojo::GetSensorForKeyboardBacklight() {
  return base_sensor_.sensor;
}

AmbientLightSensorManagerMojo::AmbientLightSensorManagerMojo(
    PrefsInterface* prefs) {
  prefs->GetInt64(kHasAmbientLightSensorPref, &num_sensors_);
  if (num_sensors_ <= 0)
    return;

  CHECK(prefs->GetBool(kAllowAmbientEQ, &allow_ambient_eq_))
      << "Failed to read pref " << kAllowAmbientEQ;

  if (num_sensors_ == 1) {
    sensors_.push_back(std::make_unique<AmbientLightSensor>());

    lid_sensor_.sensor = base_sensor_.sensor = sensors_[0].get();

    return;
  }

  sensors_.push_back(std::make_unique<AmbientLightSensor>());
  lid_sensor_.sensor = sensors_[0].get();

  sensors_.push_back(std::make_unique<AmbientLightSensor>());
  base_sensor_.sensor = sensors_[1].get();
}

AmbientLightSensorManagerMojo::~AmbientLightSensorManagerMojo() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sensors_.clear();
  lights_.clear();
  sensor_service_remote_.reset();
  sensor_hal_client_.reset();
}

bool AmbientLightSensorManagerMojo::HasColorSensor() {
  for (const auto& sensor : sensors_) {
    if (sensor->IsColorSensor())
      return true;
  }
  return false;
}

void AmbientLightSensorManagerMojo::BindSensorHalClient(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
    OnMojoDisconnectCallback on_mojo_disconnect_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!sensor_hal_client_.is_bound());

  if (num_sensors_ <= 0) {
    // Doesn't need any ambient light sensor.
    return;
  }

  sensor_hal_client_.Bind(std::move(pending_receiver));
  sensor_hal_client_.set_disconnect_handler(base::BindOnce(
      &AmbientLightSensorManagerMojo::OnSensorHalClientDisconnect,
      base::Unretained(this)));

  on_mojo_disconnect_callback_ = std::move(on_mojo_disconnect_callback);
}

void AmbientLightSensorManagerMojo::SetUpChannel(
    mojo::PendingRemote<cros::mojom::SensorService> sensor_service_remote) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GT(num_sensors_, 0);

  if (sensor_service_remote_.is_bound()) {
    LOG(ERROR)
        << "Received the second SensorService Remote while the first one is "
           "still bound. Workaround: reset the first Remote, SensorDevice "
           "Remotes and SensorDeviceSamplesObserver Receivers.";
    ResetSensorService();
  }

  sensor_service_remote_.Bind(std::move(sensor_service_remote));
  sensor_service_remote_.set_disconnect_handler(
      base::BindOnce(&AmbientLightSensorManagerMojo::OnSensorServiceDisconnect,
                     base::Unretained(this)));

  bool need_device_ids = false;
  if (num_sensors_ == 1) {
    if (lid_sensor_.iio_device_id.has_value()) {
      // Use the original device.
      SetSensorDeviceMojo(&lid_sensor_, allow_ambient_eq_);

      auto& light = lights_[lid_sensor_.iio_device_id.value()];
      if (!light.name.has_value() ||
          light.name.value().compare(kCrosECLightName) != 0) {
        // Even though this device is not cros-ec-light, cros-ec-light may
        // exist, so we will still look for cros-ec-light later.
        need_device_ids = true;
      }
    } else {
      need_device_ids = true;
    }
  } else {  // num_sensors_ >= 2
    // The two cros-ec-lights on lid and base should exist. Therefore, the
    // potential existing acpi-als is ignored.
    if (lid_sensor_.iio_device_id.has_value())
      SetSensorDeviceMojo(&lid_sensor_, allow_ambient_eq_);
    else
      need_device_ids = true;

    if (base_sensor_.iio_device_id.has_value())
      SetSensorDeviceMojo(&base_sensor_, /*allow_ambient_eq=*/false);
    else
      need_device_ids = true;
  }

  if (need_device_ids) {
    sensor_service_remote_->RegisterNewDevicesObserver(
        new_devices_observer_.BindNewPipeAndPassRemote());
    new_devices_observer_.set_disconnect_handler(base::BindOnce(
        &AmbientLightSensorManagerMojo::OnNewDevicesObserverDisconnect,
        base::Unretained(this)));

    QueryDevices();
  }
}

void AmbientLightSensorManagerMojo::OnNewDeviceAdded(
    int32_t iio_device_id, const std::vector<cros::mojom::DeviceType>& types) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GT(num_sensors_, 0);

  if (std::find(types.begin(), types.end(), cros::mojom::DeviceType::LIGHT) ==
      types.end()) {
    // Not a light sensor. Ignoring this device.
    return;
  }

  if (lights_.find(iio_device_id) != lights_.end()) {
    // Has already added this device.
    return;
  }

  auto& light = lights_[iio_device_id];

  sensor_service_remote_->GetDevice(iio_device_id,
                                    light.remote.BindNewPipeAndPassReceiver());
  light.remote.set_disconnect_with_reason_handler(
      base::BindOnce(&AmbientLightSensorManagerMojo::OnSensorDeviceDisconnect,
                     base::Unretained(this), iio_device_id));

  if (num_sensors_ == 1) {
    light.remote->GetAttributes(
        std::vector<std::string>{cros::mojom::kDeviceName},
        base::BindOnce(&AmbientLightSensorManagerMojo::GetNameCallback,
                       base::Unretained(this), iio_device_id));
  } else {  // num_sensors_ >= 2
    light.remote->GetAttributes(
        std::vector<std::string>{cros::mojom::kDeviceName,
                                 cros::mojom::kLocation},
        base::BindOnce(
            &AmbientLightSensorManagerMojo::GetNameAndLocationCallback,
            base::Unretained(this), iio_device_id));
  }
}

void AmbientLightSensorManagerMojo::OnSensorHalClientDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(on_mojo_disconnect_callback_);

  LOG(WARNING) << "SensorHalClient connection lost";

  ResetSensorService();
  sensor_hal_client_.reset();

  std::move(on_mojo_disconnect_callback_).Run();
}

void AmbientLightSensorManagerMojo::OnSensorServiceDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(WARNING) << "SensorService connection lost";

  ResetSensorService();
}

void AmbientLightSensorManagerMojo::ResetSensorService() {
  for (auto& sensor : sensors_)
    sensor->SetDelegate(nullptr);

  for (auto& light : lights_)
    light.second.remote.reset();

  new_devices_observer_.reset();
  sensor_service_remote_.reset();
}

void AmbientLightSensorManagerMojo::ResetStates() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& sensor : sensors_)
    sensor->SetDelegate(nullptr);

  lid_sensor_.iio_device_id = base_sensor_.iio_device_id = base::nullopt;
  lights_.clear();

  if (sensor_service_remote_.is_bound())
    QueryDevices();
}

void AmbientLightSensorManagerMojo::QueryDevices() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(sensor_service_remote_.is_bound());

  sensor_service_remote_->GetDeviceIds(
      cros::mojom::DeviceType::LIGHT,
      base::BindOnce(&AmbientLightSensorManagerMojo::GetDeviceIdsCallback,
                     base::Unretained(this)));
}

void AmbientLightSensorManagerMojo::OnNewDevicesObserverDisconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(ERROR)
      << "OnNewDevicesObserverDisconnect, resetting SensorService as "
         "IIO Service should be destructed and waiting for it to relaunch.";
  ResetSensorService();
}

void AmbientLightSensorManagerMojo::OnSensorDeviceDisconnect(
    int32_t id, uint32_t custom_reason_code, const std::string& description) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const auto reason = static_cast<cros::mojom::SensorDeviceDisconnectReason>(
      custom_reason_code);
  LOG(WARNING) << "OnSensorDeviceDisconnect: " << id << ", reason: " << reason
               << ", description: " << description;

  switch (reason) {
    case cros::mojom::SensorDeviceDisconnectReason::IIOSERVICE_CRASHED:
      ResetSensorService();
      break;

    case cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED:
      if (lid_sensor_.iio_device_id == id || base_sensor_.iio_device_id == id) {
        // Reset usages & states, and restart the mojo devices initialization.
        ResetStates();
      } else {
        // This light sensor is not in use.
        lights_.erase(id);
      }
      break;
  }
}

void AmbientLightSensorManagerMojo::GetDeviceIdsCallback(
    const std::vector<int32_t>& iio_device_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GT(num_sensors_, 0);

  if (iio_device_ids.empty())
    return;

  if (num_sensors_ == 1) {
    DCHECK_EQ(sensors_.size(), 1);

    for (int32_t id : iio_device_ids) {
      auto& light = lights_[id];
      DCHECK(!light.remote.is_bound());

      if (light.ignored || light.name.has_value())
        continue;

      sensor_service_remote_->GetDevice(
          id, light.remote.BindNewPipeAndPassReceiver());
      light.remote.set_disconnect_with_reason_handler(base::BindOnce(
          &AmbientLightSensorManagerMojo::OnSensorDeviceDisconnect,
          base::Unretained(this), id));

      light.remote->GetAttributes(
          std::vector<std::string>{cros::mojom::kDeviceName},
          base::BindOnce(&AmbientLightSensorManagerMojo::GetNameCallback,
                         base::Unretained(this), id));
    }

    return;
  }

  DCHECK_GE(num_sensors_, 2);

  for (int32_t id : iio_device_ids) {
    auto& light = lights_[id];
    DCHECK(!light.remote.is_bound());

    if (light.ignored || light.name.has_value() || light.location.has_value())
      continue;

    sensor_service_remote_->GetDevice(
        id, light.remote.BindNewPipeAndPassReceiver());
    light.remote.set_disconnect_with_reason_handler(
        base::BindOnce(&AmbientLightSensorManagerMojo::OnSensorDeviceDisconnect,
                       base::Unretained(this), id));

    light.remote->GetAttributes(
        std::vector<std::string>{cros::mojom::kDeviceName,
                                 cros::mojom::kLocation},
        base::BindOnce(
            &AmbientLightSensorManagerMojo::GetNameAndLocationCallback,
            base::Unretained(this), id));
  }
}

void AmbientLightSensorManagerMojo::GetNameCallback(
    int32_t id, const std::vector<base::Optional<std::string>>& values) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(num_sensors_, 1);

  auto& light = lights_[id];
  DCHECK(light.remote.is_bound());

  if (values.empty()) {
    LOG(ERROR) << "Sensor values doesn't contain the name attribute.";
    light.ignored = true;
    light.remote.reset();
    return;
  }

  if (values.size() != 1) {
    LOG(WARNING) << "Sensor values contain more than the name attribute. Size: "
                 << values.size();
  }

  light.name = values[0];
  if (light.name.has_value() &&
      light.name.value().compare(kCrosECLightName) == 0) {
    LOG(INFO) << "Using ALS with id: " << id
              << ", name: " << light.name.value();

    lid_sensor_.iio_device_id = base_sensor_.iio_device_id = id;
    auto delegate = AmbientLightSensorDelegateMojo::Create(
        id, std::move(light.remote), allow_ambient_eq_);
    lid_sensor_.sensor->SetDelegate(std::move(delegate));

    // Found cros-ec-light. Other devices are not needed.
    AllDevicesFound();

    return;
  }

  // Not cros-ec-light
  if (!light.name.has_value() ||
      light.name.value().compare(kAcpiAlsName) != 0) {
    LOG(WARNING) << "Unexpected or empty light name: "
                 << light.name.value_or("");
  }

  if (lid_sensor_.iio_device_id.has_value()) {
    VLOG(1) << "Already have another light sensor with name: "
            << lights_[lid_sensor_.iio_device_id.value()].name.value_or("");
    light.ignored = true;
    light.remote.reset();
    return;
  }

  LOG(INFO) << "Using ALS with id: " << id
            << ", name: " << light.name.value_or("null");

  lid_sensor_.iio_device_id = id;
  auto delegate = AmbientLightSensorDelegateMojo::Create(
      id, std::move(light.remote), allow_ambient_eq_);
  lid_sensor_.sensor->SetDelegate(std::move(delegate));
}

void AmbientLightSensorManagerMojo::GetNameAndLocationCallback(
    int32_t id, const std::vector<base::Optional<std::string>>& values) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GE(num_sensors_, 2);

  auto& light = lights_[id];
  DCHECK(light.remote.is_bound());

  if (values.size() < 2) {
    LOG(ERROR) << "Sensor is missing name or location attribute.";
    light.ignored = true;
    light.remote.reset();
    return;
  }

  if (values.size() > 2) {
    LOG(WARNING)
        << "Sensor values contain more than name and location attribute. Size: "
        << values.size();
  }

  light.name = values[0];
  if (!light.name.has_value() ||
      light.name.value().compare(kCrosECLightName) != 0) {
    LOG(ERROR) << "Not " << kCrosECLightName
               << ", sensor name: " << light.name.value_or("");
    light.ignored = true;
    light.remote.reset();
    return;
  }

  const base::Optional<std::string>& location = values[1];
  if (!location.has_value()) {
    LOG(WARNING) << "Sensor doesn't have the location attribute: " << id;
    SetSensorDeviceAtLocation(id, SensorLocation::UNKNOWN);
    return;
  }

  if (location.value() == cros::mojom::kLocationLid) {
    SetSensorDeviceAtLocation(id, SensorLocation::LID);
  } else if (location.value() == cros::mojom::kLocationBase) {
    SetSensorDeviceAtLocation(id, SensorLocation::BASE);
  } else {
    LOG(ERROR) << "Invalid sensor " << id << ", location: " << location.value();
    SetSensorDeviceAtLocation(id, SensorLocation::UNKNOWN);
  }
}

void AmbientLightSensorManagerMojo::SetSensorDeviceAtLocation(
    int32_t id, SensorLocation location) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GE(num_sensors_, 2);

  auto& light = lights_[id];
  DCHECK(!light.location.has_value() || light.location == location);
  light.location = location;

  if (location == SensorLocation::LID &&
      (!lid_sensor_.iio_device_id.has_value() ||
       lid_sensor_.iio_device_id.value() == id)) {
    LOG(INFO) << "Using Lid ALS with id: " << id;

    lid_sensor_.iio_device_id = id;

    auto delegate = AmbientLightSensorDelegateMojo::Create(
        id, std::move(light.remote), allow_ambient_eq_);
    lid_sensor_.sensor->SetDelegate(std::move(delegate));
  } else if (location == SensorLocation::BASE &&
             (!base_sensor_.iio_device_id.has_value() ||
              base_sensor_.iio_device_id.value() == id)) {
    LOG(INFO) << "Using Base ALS with id: " << id;

    base_sensor_.iio_device_id = id;

    auto delegate = AmbientLightSensorDelegateMojo::Create(
        id, std::move(light.remote),
        /*enable_color_support=*/false);  // BASE sensor is not expected to be
                                          // used for AEQ.
    base_sensor_.sensor->SetDelegate(std::move(delegate));
  }

  if (lid_sensor_.iio_device_id.has_value() &&
      base_sensor_.iio_device_id.has_value()) {
    // Has found the two cros-ec-lights. Don't need other devices.
    AllDevicesFound();
  }

  light.remote.reset();
}

void AmbientLightSensorManagerMojo::AllDevicesFound() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Remove and ignore remaining remotes as they're not needed anymore.
  for (auto& light : lights_) {
    if (!light.second.remote.is_bound())
      continue;

    light.second.ignored = true;
    light.second.remote.reset();
  }

  // Don't need to wait for other devices.
  new_devices_observer_.reset();
}

void AmbientLightSensorManagerMojo::SetSensorDeviceMojo(Sensor* sensor,
                                                        bool allow_ambient_eq) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(sensor_service_remote_.is_bound());
  DCHECK(sensor->iio_device_id.has_value());

  mojo::Remote<cros::mojom::SensorDevice> sensor_device_remote;
  sensor_service_remote_->GetDevice(
      sensor->iio_device_id.value(),
      sensor_device_remote.BindNewPipeAndPassReceiver());

  sensor_device_remote.set_disconnect_with_reason_handler(
      base::BindOnce(&AmbientLightSensorManagerMojo::OnSensorDeviceDisconnect,
                     base::Unretained(this), sensor->iio_device_id.value()));

  std::unique_ptr<AmbientLightSensorDelegateMojo> delegate =
      AmbientLightSensorDelegateMojo::Create(sensor->iio_device_id.value(),
                                             std::move(sensor_device_remote),
                                             allow_ambient_eq);
  sensor->sensor->SetDelegate(std::move(delegate));
}

}  // namespace system
}  // namespace power_manager
