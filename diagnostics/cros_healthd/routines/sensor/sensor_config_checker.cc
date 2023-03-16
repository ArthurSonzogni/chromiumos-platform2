// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensor_config_checker.h"

#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <iioservice/mojo/sensor.mojom.h>

#include "diagnostics/cros_healthd/system/system_config.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"

namespace {

// Return true if the sensor is accelerometer, gyroscope or magnetometer.
bool IsTargetType(const std::vector<cros::mojom::DeviceType>& types) {
  for (const auto& type : types) {
    if (type == cros::mojom::DeviceType::ACCEL ||
        type == cros::mojom::DeviceType::ANGLVEL ||
        type == cros::mojom::DeviceType::MAGN)
      return true;
  }
  return false;
}

// Check if the |has_sensor| value in static config is consistent with the
// actual |is_present|. Return true if config is missing.
bool IsConfigConsistent(std::optional<bool> has_sensor, bool is_present) {
  return !has_sensor.has_value() || (has_sensor.value() == is_present);
}

// Convert the enum to readable string.
std::string Convert(diagnostics::SensorConfig sensor) {
  switch (sensor) {
    case diagnostics::SensorConfig::kBaseAccelerometer:
      return "base accelerometer";
    case diagnostics::SensorConfig::kBaseGyroscope:
      return "base gyroscope";
    case diagnostics::SensorConfig::kBaseMagnetometer:
      return "base magnetometer";
    case diagnostics::SensorConfig::kLidAccelerometer:
      return "lid accelerometer";
    case diagnostics::SensorConfig::kLidGyroscope:
      return "lid gyroscope";
    case diagnostics::SensorConfig::kLidMagnetometer:
      return "lid magnetometer";
  }
}

}  // namespace

namespace diagnostics {

SensorConfigChecker::SensorConfigChecker(
    MojoService* const mojo_service, SystemConfigInterface* const system_config)
    : mojo_service_(mojo_service), system_config_(system_config) {
  CHECK(mojo_service_);
  CHECK(system_config_);
}

SensorConfigChecker::~SensorConfigChecker() = default;

void SensorConfigChecker::VerifySensorInfo(
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        ids_types,
    base::OnceCallback<void(bool)> on_finish) {
  CallbackBarrier barrier{
      base::BindOnce(&SensorConfigChecker::CheckSystemConfig,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish))};
  for (const auto& [sensor_id, sensor_types] : ids_types) {
    if (!IsTargetType(sensor_types))
      continue;

    // Get the sensor location.
    mojo_service_->GetSensorDevice(sensor_id)->GetAttributes(
        {cros::mojom::kLocation},
        barrier.Depend(
            base::BindOnce(&SensorConfigChecker::HandleSensorLocationResponse,
                           weak_ptr_factory_.GetWeakPtr(), sensor_types)));
  }
}

void SensorConfigChecker::HandleSensorLocationResponse(
    const std::vector<cros::mojom::DeviceType>& sensor_types,
    const std::vector<std::optional<std::string>>& attributes) {
  if (attributes.size() != 1 || !attributes[0].has_value()) {
    LOG(ERROR) << "Failed to access sensor location.";
    return;
  }

  const auto& location = attributes[0].value();
  for (const auto& type : sensor_types) {
    if (type == cros::mojom::DeviceType::ACCEL) {
      if (location == cros::mojom::kLocationBase)
        iio_sensors_.insert(kBaseAccelerometer);
      else if (location == cros::mojom::kLocationLid)
        iio_sensors_.insert(kLidAccelerometer);
    } else if (type == cros::mojom::DeviceType::ANGLVEL) {
      if (location == cros::mojom::kLocationBase)
        iio_sensors_.insert(kBaseGyroscope);
      else if (location == cros::mojom::kLocationLid)
        iio_sensors_.insert(kLidGyroscope);
    } else if (type == cros::mojom::DeviceType::MAGN) {
      if (location == cros::mojom::kLocationBase)
        iio_sensors_.insert(kBaseMagnetometer);
      else if (location == cros::mojom::kLocationLid)
        iio_sensors_.insert(kLidMagnetometer);
    }
  }
}

void SensorConfigChecker::CheckSystemConfig(
    base::OnceCallback<void(bool)> on_finish, bool all_callbacks_called) {
  if (!all_callbacks_called) {
    LOG(ERROR) << "Some callbacks are not called successfully";
    std::move(on_finish).Run(false);
    return;
  }

  for (const auto& sensor :
       {kBaseAccelerometer, kLidAccelerometer, kBaseGyroscope, kLidGyroscope,
        kBaseMagnetometer, kLidMagnetometer}) {
    if (!IsConfigConsistent(system_config_->HasSensor(sensor),
                            iio_sensors_.count(sensor))) {
      LOG(ERROR) << "Failed to verify " << Convert(sensor);
      std::move(on_finish).Run(false);
      return;
    }
  }

  std::move(on_finish).Run(true);
}

}  // namespace diagnostics
