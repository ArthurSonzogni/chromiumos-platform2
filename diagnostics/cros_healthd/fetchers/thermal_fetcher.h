// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_THERMAL_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_THERMAL_FETCHER_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

// Fetches thermal info and pass the result to the callback. Returns either a
// structure with the thermal information or the error that occurred fetching
// the information.
using FetchThermalInfoCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::ThermalResultPtr)>;

// Directory containing thermal zone info.
inline constexpr char kRelativeThermalDir[] = "sys/class/thermal/";

// Matches all thermal zones in |kRelativeThermalDir|.
const char kThermalZonePattern[] = "thermal_zone*";

// Files read from the thermal zone directory.
inline constexpr char kThermalZoneTypeFileName[] = "type";
inline constexpr char kThermalZoneTempFileName[] = "temp";

void FetchThermalInfo(Context* context, FetchThermalInfoCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_THERMAL_FETCHER_H_
