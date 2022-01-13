// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_

#include <list>
#include <string>

namespace diagnostics {

// The path used to check a device's master configuration hardware properties.
inline constexpr auto kHardwarePropertiesPath = "/hardware-properties";
// The master configuration property that specifies a device's PSU type.
inline constexpr auto kPsuTypeProperty = "psu-type";
// The path used to check a device's master configuration cros_healthd battery
// properties.
inline constexpr auto kBatteryPropertiesPath = "/cros-healthd/battery";
// The master configuration property that indicates whether a device has Smart
// Battery info.
inline constexpr auto kHasSmartBatteryInfoProperty = "has-smart-battery-info";
// The master configuration property that indicates whether a device has a
// backlight.
inline constexpr auto kHasBacklightProperty = "has-backlight";
// The path used to check a device's master configuration cros_healthd vpd
// properties.
inline constexpr auto kCachedVpdPropertiesPath = "/cros-healthd/cached-vpd";
// The master configuration property that indicates whether a device has a
// sku number in the VPD fields.
inline constexpr auto kHasSkuNumberProperty = "has-sku-number";
// NVME utility program path relative to the root directory.
inline constexpr auto kNvmeToolPath = "usr/sbin/nvme";
// Linux device path relative to the root directory.
inline constexpr auto kDevicePath = "dev";
// Smartctl utility program path relative to the root directory.
inline constexpr auto kSmartctlToolPath = "usr/sbin/smartctl";
// Fio utility program path relative to the root directory.
inline constexpr auto kFioToolPath = "usr/bin/fio";
// The path to check a device's master configuration ARC build properties.
inline constexpr auto kArcBuildPropertiesPath = "/arc/build-properties";
// The master configuration property that specifies a device's marketing name.
inline constexpr auto kMarketingNameProperty = "marketing-name";
// The root path of master configuration.
inline constexpr auto kRootPath = "/";
// The master configuration property that specifies a device's code name.
inline constexpr auto kCodeNameProperty = "name";

// Returns a list of wilco board names.
inline const std::list<std::string> GetWilcoBoardNames() {
  return {"sarien", "drallion"};
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
