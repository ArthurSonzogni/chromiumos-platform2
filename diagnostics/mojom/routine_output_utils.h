// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_MOJOM_ROUTINE_OUTPUT_UTILS_H_
#define DIAGNOSTICS_MOJOM_ROUTINE_OUTPUT_UTILS_H_

#include <base/values.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {

base::Value::Dict ParseAudioDriverDetail(
    const ash::cros_healthd::mojom::AudioDriverRoutineDetailPtr&
        audio_driver_detail);

base::Value::Dict ParseBluetoothDiscoveryDetail(
    const ash::cros_healthd::mojom::BluetoothDiscoveryRoutineDetailPtr&
        bluetooth_discovery_detail);

base::Value::Dict ParseBluetoothPowerDetail(
    const ash::cros_healthd::mojom::BluetoothPowerRoutineDetailPtr&
        bluetooth_power_detail);

base::Value::Dict ParseUfsLifetimeDetail(
    const ash::cros_healthd::mojom::UfsLifetimeRoutineDetailPtr&
        ufs_lifetime_detail);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_MOJOM_ROUTINE_OUTPUT_UTILS_H_
