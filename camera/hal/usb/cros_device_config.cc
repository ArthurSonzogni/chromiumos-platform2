/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/cros_device_config.h"

#include <base/sys_info.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kCrosConfigCameraPath[] = "/camera";
constexpr char kCrosConfigLegacyUsbKey[] = "legacy-usb";

}  // namespace

CrosDeviceConfig::CrosDeviceConfig() {}

CrosDeviceConfig::~CrosDeviceConfig() {}

CrosDeviceConfig CrosDeviceConfig::Get() {
  CrosDeviceConfig res = {};
  brillo::CrosConfig cros_config;

  if (!cros_config.Init()) {
    LOGF(ERROR) << "Failed to initialize CrOS config";
    return res;
  }

  if (!cros_config.GetString("/", "name", &res.model_name)) {
    LOGF(ERROR) << "Failed to get model name of CrOS device";
    return res;
  }

  std::string use_legacy_usb;
  if (cros_config.GetString(kCrosConfigCameraPath, kCrosConfigLegacyUsbKey,
                            &use_legacy_usb)) {
    if (use_legacy_usb == "true") {
      LOGF(INFO) << "The CrOS device is marked to have v1 camera devices";
    }
    res.is_v1_device = use_legacy_usb == "true";
  } else {
    res.is_v1_device = false;
  }

  res.is_initialized = true;
  return res;
}

}  // namespace cros
