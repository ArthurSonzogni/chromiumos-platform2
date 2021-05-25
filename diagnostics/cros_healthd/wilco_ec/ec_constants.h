// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_WILCO_EC_EC_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_WILCO_EC_EC_CONSTANTS_H_

#include <cstdint>

namespace diagnostics {

// Folder path exposed by sysfs EC driver.
extern const char kEcDriverSysfsPath[];

// Folder path to EC properties exposed by sysfs EC driver. Relative path to
// |kEcDriverSysfsPath|.
extern const char kEcDriverSysfsPropertiesPath[];

// Max request and response payload size for EC telemetry command.
extern const int64_t kEcGetTelemetryPayloadMaxSize;

// Devfs node exposed by EC driver to EC telemetry data.
extern const char kEcGetTelemetryFilePath[];

// EC event file path.
extern const char kEcEventFilePath[];

// The driver is expected to populate the |kEcEventFilePath| file, therefore
// this constant holds the specific flag for use with poll().
extern const int16_t kEcEventFilePollEvents;

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_WILCO_EC_EC_CONSTANTS_H_
