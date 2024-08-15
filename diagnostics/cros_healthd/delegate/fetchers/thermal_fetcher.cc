// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/thermal_fetcher.h"

#include <fcntl.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <chromeos/ec/ec_commands.h>
#include <libec/thermal/get_memmap_temp_b_command.h>
#include <libec/thermal/get_memmap_temp_command.h>
#include <libec/thermal/get_memmap_thermal_version_command.h>
#include <libec/thermal/temp_sensor_get_info_command.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

double KelvinToCelsius(int temperature_kelvin) {
  return static_cast<double>(temperature_kelvin) - 273.15;
}

}  // namespace

std::optional<std::vector<mojom::ThermalSensorInfoPtr>>
FetchEcThermalSensors() {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));

  std::vector<mojom::ThermalSensorInfoPtr> thermal_sensors;

  ec::GetMemmapThermalVersionCommand get_version{};
  if (!get_version.Run(cros_fd.get()) ||
      !get_version.ThermalVersion().has_value()) {
    LOG(ERROR) << "Failed to read thermal sensor version";
    return std::nullopt;
  }

  for (uint8_t sensor_idx = 0; sensor_idx < EC_MAX_TEMP_SENSOR_ENTRIES;
       ++sensor_idx) {
    int temperature_offset;

    if (sensor_idx < EC_TEMP_SENSOR_ENTRIES) {
      ec::GetMemmapTempCommand get_temp{sensor_idx};
      if (!get_temp.Run(cros_fd.get()) || !get_temp.Temp().has_value()) {
        LOG(ERROR) << "Failed to read temperature for thermal sensor idx: "
                   << static_cast<int>(sensor_idx);
        continue;
      }
      temperature_offset = get_temp.Temp().value();
    } else if (get_version.ThermalVersion() >= 2) {
      // If the sensor index is larger than or equal to EC_TEMP_SENSOR_ENTRIES,
      // the temperature should be read from the second bank, which is only
      // supported in thermal version >= 2.
      ec::GetMemmapTempBCommand get_temp{sensor_idx};
      if (!get_temp.Run(cros_fd.get()) || !get_temp.Temp().has_value()) {
        LOG(ERROR) << "Failed to read temperature for thermal sensor idx: "
                   << static_cast<int>(sensor_idx);
        continue;
      }
      temperature_offset = get_temp.Temp().value();
    } else {
      // The sensor index is located in the second bank, but EC does not support
      // reading from it. Break and return only results from the first bank.
      LOG(WARNING) << "EC does not support reading more thermal sensors";
      break;
    }

    // TODO(b/304654144): Some boards without temperature sensors return 0
    // instead of EC_TEMP_SENSOR_NOT_PRESENT. Treat 0 (-73.15°C) as indicator of
    // absent temperature sensor.
    if (temperature_offset == EC_TEMP_SENSOR_NOT_PRESENT ||
        temperature_offset == 0) {
      break;
    }
    if (temperature_offset == EC_TEMP_SENSOR_ERROR) {
      LOG(ERROR) << "Error in thermal sensor idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }
    if (temperature_offset == EC_TEMP_SENSOR_NOT_POWERED) {
      LOG(ERROR) << "Thermal sensor not powered, idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }
    if (temperature_offset == EC_TEMP_SENSOR_NOT_CALIBRATED) {
      LOG(ERROR) << "Thermal sensor not calibrated, idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }

    ec::TempSensorGetInfoCommand get_info{sensor_idx};
    if (!get_info.Run(cros_fd.get()) || !get_info.SensorName().has_value()) {
      LOG(ERROR) << "Failed to read sensor info for thermal sensor idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }

    auto sensor_info = mojom::ThermalSensorInfo::New();
    sensor_info->temperature_celsius =
        KelvinToCelsius(temperature_offset + EC_TEMP_SENSOR_OFFSET);
    sensor_info->name = get_info.SensorName().value();
    sensor_info->source = mojom::ThermalSensorInfo::ThermalSensorSource::kEc;

    thermal_sensors.push_back(std::move(sensor_info));
  }

  return thermal_sensors;
}

}  // namespace diagnostics
