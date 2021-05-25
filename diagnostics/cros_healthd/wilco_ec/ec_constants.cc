// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/wilco_ec/ec_constants.h"

#include <poll.h>

namespace diagnostics {

const char kEcDriverSysfsPath[] = "sys/bus/platform/devices/GOOG000C:00/";

const char kEcDriverSysfsPropertiesPath[] = "properties/";

const int64_t kEcGetTelemetryPayloadMaxSize = 32;

const char kEcGetTelemetryFilePath[] = "dev/wilco_telem0";

const char kEcEventFilePath[] = "dev/wilco_event0";

const int16_t kEcEventFilePollEvents = POLLIN;

}  // namespace diagnostics
