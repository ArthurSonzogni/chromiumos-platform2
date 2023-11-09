// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BATTERY_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BATTERY_FETCHER_H_

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

class Context;

// Fetch battery info. Some info is fetched via powerd, while Smart Battery info
// is collected from ectool via debugd.
ash::cros_healthd::mojom::BatteryResultPtr FetchBatteryInfo(Context* context);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BATTERY_FETCHER_H_
