// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_THERMAL_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_THERMAL_FETCHER_H_

#include <optional>
#include <vector>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

std::optional<std::vector<ash::cros_healthd::mojom::ThermalSensorInfoPtr>>
FetchEcThermalSensors();

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_THERMAL_FETCHER_H_
