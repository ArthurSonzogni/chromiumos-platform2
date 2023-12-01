// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_MOJOM_ROUTINE_OUTPUT_UTILS_H_
#define DIAGNOSTICS_MOJOM_ROUTINE_OUTPUT_UTILS_H_

#include <base/values.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::AudioDriverRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::BluetoothDiscoveryRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::BluetoothPairingRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::BluetoothPowerRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::BluetoothScanningRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::UfsLifetimeRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::FanRoutineDetailPtr& detail);

base::Value::Dict ConvertToValue(
    const ash::cros_healthd::mojom::CameraAvailabilityRoutineDetailPtr& detail);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_MOJOM_ROUTINE_OUTPUT_UTILS_H_
