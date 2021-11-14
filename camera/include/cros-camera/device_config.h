/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_DEVICE_CONFIG_H_
#define CAMERA_INCLUDE_CROS_CAMERA_DEVICE_CONFIG_H_

#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>

#include "cros-camera/export.h"

namespace cros {

// The physical transmission interface, or bus, of a camera.
enum class Interface {
  kUsb,
  kMipi,
};

// The direction a camera faces. The definition should match
// camera_metadata_enum_android_lens_facing_t in camera_metadata_tags.h.
enum class LensFacing {
  kFront,
  kBack,
  kExternal,
};

// This class wraps the brillo::CrosConfig and stores the required values.
class CROS_CAMERA_EXPORT DeviceConfig {
 public:
  static base::Optional<DeviceConfig> Create();

  bool IsV1Device() const { return is_v1_device_; }

  // Gets the model name of the device.
  const std::string& GetModelName() const { return model_name_; }

  // Gets the total number of built-in cameras on the device, or nullopt if the
  // information is not available.
  base::Optional<int> GetBuiltInCameraCount() const { return count_; }

  // Gets the total number of cameras on the given interface |interface|, or
  // nullopt if the information is not available.
  base::Optional<int> GetCameraCount(Interface interface) const;

  // Gets camera orientation of the camera facing the given |facing| direction,
  // or nullopt if the information is not available.
  base::Optional<int> GetOrientationFromFacing(LensFacing facing) const;

 private:
  struct Device {
    Interface interface;
    LensFacing facing;
    int orientation;
  };

  DeviceConfig() = default;

  bool is_v1_device_;
  std::string model_name_;
  // The number of built-in cameras, or |base::nullopt| when this information is
  // not available.
  base::Optional<int> count_;
  // Detailed topology of the camera devices, or empty when this information is
  // not available. |count_| has value |devices_.size()| if |devices_| is not
  // empty.
  std::vector<Device> devices_;
};

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_DEVICE_CONFIG_H_
