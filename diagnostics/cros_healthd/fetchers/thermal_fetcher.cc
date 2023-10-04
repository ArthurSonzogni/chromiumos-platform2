// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/thermal_fetcher.h"

#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::optional<mojom::ThermalSensorInfoPtr> ParseThermalSensorInfo(
    const base::FilePath& thermal_path) {
  auto sensor_info = mojom::ThermalSensorInfo::New();
  double temperature_millicelsius;
  if (!ReadInteger(thermal_path.AppendASCII(kThermalZoneTempFileName),
                   base::StringToDouble, &temperature_millicelsius)) {
    return std::nullopt;
  }
  sensor_info->temperature_celsius = temperature_millicelsius / 1000;
  if (!ReadAndTrimString(thermal_path.AppendASCII(kThermalZoneTypeFileName),
                         &sensor_info->name)) {
    return std::nullopt;
  }
  sensor_info->source = mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs;
  return sensor_info;
}

std::vector<mojom::ThermalSensorInfoPtr> GetSysfsThermalSensors(
    Context* context) {
  std::vector<mojom::ThermalSensorInfoPtr> sysfs_sensors;
  base::FileEnumerator thermal_enumerator(
      context->root_dir().AppendASCII(kRelativeThermalDir),
      /*recursive=*/false, base::FileEnumerator::DIRECTORIES,
      kThermalZonePattern);
  for (auto thermal_path = thermal_enumerator.Next(); !thermal_path.empty();
       thermal_path = thermal_enumerator.Next()) {
    auto thermal_sensor_info = ParseThermalSensorInfo(thermal_path);
    if (thermal_sensor_info.has_value()) {
      sysfs_sensors.push_back(std::move(thermal_sensor_info.value()));
    }
  }
  return sysfs_sensors;
}

void HandleGetEcThermalSensors(
    std::vector<mojom::ThermalSensorInfoPtr> result,
    FetchThermalInfoCallback callback,
    std::vector<mojom::ThermalSensorInfoPtr> ec_sensors,
    const std::optional<std::string>& error) {
  // As an attempt to get perform best-effort telemetry, we append ec_sensor
  // information even if error is encountered.
  for (auto& ec_sensor : ec_sensors) {
    result.emplace_back(std::move(ec_sensor));
  }
  std::move(callback).Run(mojom::ThermalResult::NewThermalInfo(
      mojom::ThermalInfo::New(std::move(result))));
}

}  // namespace

void FetchThermalInfo(Context* context, FetchThermalInfoCallback callback) {
  std::vector<mojom::ThermalSensorInfoPtr> result =
      GetSysfsThermalSensors(context);
  context->executor()->GetEcThermalSensors(base::BindOnce(
      &HandleGetEcThermalSensors, std::move(result), std::move(callback)));
}

}  // namespace diagnostics
