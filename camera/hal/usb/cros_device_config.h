/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_
#define CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_

#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "hal/usb/common_types.h"

namespace cros {

enum class Interface {
  kUsb,
  kMipi,
};

// This class wraps the brillo::CrosConfig and stores the required values.
class CrosDeviceConfig {
 public:
  static std::unique_ptr<CrosDeviceConfig> Create();

  bool IsV1Device() const { return is_v1_device_; }
  const std::string& GetModelName() const { return model_name_; }
  base::Optional<int> GetCameraCount(Interface interface) const;
  base::Optional<int> GetOrientationFromFacing(LensFacing facing) const;

 private:
  struct Device {
    Interface interface;
    LensFacing facing;
    int orientation;
  };

  CrosDeviceConfig() = default;

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

#endif  // CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_
