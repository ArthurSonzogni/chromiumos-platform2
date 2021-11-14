/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/device_config.h"

#include <algorithm>

#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kCrosConfigCameraPath[] = "/camera";
constexpr char kCrosConfigLegacyUsbKey[] = "legacy-usb";

}  // namespace

base::Optional<DeviceConfig> DeviceConfig::Create() {
  DeviceConfig res = {};
  brillo::CrosConfig cros_config;

  if (!cros_config.Init()) {
    LOGF(ERROR) << "Failed to initialize CrOS config";
    return base::nullopt;
  }

  if (!cros_config.GetString("/", "name", &res.model_name_)) {
    LOGF(ERROR) << "Failed to get model name of CrOS device";
    return base::nullopt;
  }

  std::string use_legacy_usb;
  if (cros_config.GetString(kCrosConfigCameraPath, kCrosConfigLegacyUsbKey,
                            &use_legacy_usb)) {
    if (use_legacy_usb == "true") {
      LOGF(INFO) << "The CrOS device is marked to have v1 camera devices";
    }
    res.is_v1_device_ = use_legacy_usb == "true";
  } else {
    res.is_v1_device_ = false;
  }

  std::string count_str;
  if (cros_config.GetString("/camera", "count", &count_str)) {
    res.count_ = std::stoi(count_str);
  }

  for (int i = 0;; ++i) {
    std::string interface;
    if (!cros_config.GetString(base::StringPrintf("/camera/devices/%i", i),
                               "interface", &interface)) {
      break;
    }
    std::string facing, orientation;
    CHECK(cros_config.GetString(base::StringPrintf("/camera/devices/%i", i),
                                "facing", &facing));
    CHECK(cros_config.GetString(base::StringPrintf("/camera/devices/%i", i),
                                "orientation", &orientation));
    res.devices_.push_back(Device{
        .interface = interface == "usb" ? Interface::kUsb : Interface::kMipi,
        .facing = facing == "front" ? LensFacing::kFront : LensFacing::kBack,
        .orientation = std::stoi(orientation),
    });
  }
  if (!res.devices_.empty()) {
    CHECK(res.count_.has_value());
    CHECK_EQ(static_cast<size_t>(*res.count_), res.devices_.size());
  }

  return base::make_optional<DeviceConfig>(res);
}

base::Optional<int> DeviceConfig::GetCameraCount(Interface interface) const {
  if (!count_.has_value())
    return base::nullopt;
  // |count_| includes both MIPI and USB cameras. If |count_| is not 0, we need
  // the |devices_| information to determine the numbers.
  if (*count_ == 0)
    return 0;
  if (devices_.empty())
    return base::nullopt;
  return std::count_if(devices_.begin(), devices_.end(), [=](const Device& d) {
    return d.interface == interface;
  });
}

base::Optional<int> DeviceConfig::GetOrientationFromFacing(
    LensFacing facing) const {
  auto iter = std::find_if(devices_.begin(), devices_.end(),
                           [=](const Device& d) { return d.facing == facing; });
  if (iter == devices_.end())
    return base::nullopt;
  return iter->orientation;
}

}  // namespace cros
