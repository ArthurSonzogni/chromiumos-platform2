// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BASE_PATHS_H_
#define DIAGNOSTICS_BASE_PATHS_H_

#include "diagnostics/base/path_utils.h"

// VAR_ put the paths before the variable names so it is easier to read.
#define VAR_(path, var) inline constexpr auto var = path
#define PATH_(...) MakePathLiteral(__VA_ARGS__)

namespace diagnostics::paths {

// TODO(b/308731445): Use this file to define paths.

namespace cros_config {

VAR_(PATH_("run", "chromeos-config", "v1"), kRoot);
VAR_(PATH_("run", "chromeos-config", "test"), kTestRoot);

VAR_(PATH_("hardware-properties", "has-backlight"), kHasBacklight);
VAR_(PATH_("hardware-properties", "psu-type"), kPsuType);
VAR_(PATH_("hardware-properties", "has-privacy-screen"), kHasPrivacyScreen);
VAR_(PATH_("hardware-properties", "has-base-accelerometer"),
     kHasBaseAccelerometer);
VAR_(PATH_("hardware-properties", "has-base-gyroscope"), kHasBaseGyroscope);
VAR_(PATH_("hardware-properties", "has-base-magnetometer"),
     kHasBaseMagnetometer);
VAR_(PATH_("hardware-properties", "has-lid-accelerometer"),
     kHasLidAccelerometer);
VAR_(PATH_("hardware-properties", "has-lid-gyroscope"), kHasLidGyroscope);
VAR_(PATH_("hardware-properties", "has-lid-magnetometer"), kHasLidMagnetometer);
VAR_(PATH_("cros-healthd", "cached-vpd", "has-sku-number"), kHasSkuNumber);
VAR_(PATH_("cros-healthd", "battery", "has-smart-battery-info"),
     kHasSmartBatteryInfo);
VAR_(PATH_("cros-healthd", "routines", "fingerprint-diag", "routine-enable"),
     kFingerprintDiagRoutineEnable);
VAR_(PATH_("branding", "oem-name"), kOemName);
VAR_(PATH_("branding", "marketing-name"), kMarketingName);
VAR_(PATH_("name"), kCodeName);

}  // namespace cros_config

}  // namespace diagnostics::paths

#undef PATH_
#undef VAR_

#endif  // DIAGNOSTICS_BASE_PATHS_H_
