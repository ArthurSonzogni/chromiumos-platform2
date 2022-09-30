// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/containers/span.h>
#include <base/notreached.h>
#include <base/values.h>

#include "cros-camera/device_config.h"
#include "runtime_probe/functions/mipi_camera.h"

namespace runtime_probe {

MipiCameraFunction::DataType MipiCameraFunction::EvalImpl() const {
  auto device_config = cros::DeviceConfig::Create();
  MipiCameraFunction::DataType results;

  if (!device_config) {
    LOG(ERROR) << "Failed to get camera device config.";
    return results;
  }

  base::span<const cros::PlatformCameraInfo> cameras =
      device_config->GetPlatformCameraInfo();
  for (const auto& camera : cameras) {
    base::Value::Dict node;
    if (camera.eeprom) {
      node.Set("name", camera.sysfs_name);
      node.Set("module_id", camera.module_id());
      node.Set("sensor_id", camera.sensor_id());
    } else if (camera.v4l2_sensor) {
      node.Set("name", camera.v4l2_sensor->name);
      node.Set("vendor", camera.v4l2_sensor->vendor_id);
    } else {
      NOTREACHED() << "Unknown source of camera info.";
    }
    results.Append(std::move(node));
  }

  return results;
}

}  // namespace runtime_probe
