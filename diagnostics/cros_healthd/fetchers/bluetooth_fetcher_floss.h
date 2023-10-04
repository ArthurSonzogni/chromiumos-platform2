// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_FLOSS_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_FLOSS_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

class Context;

// This function is supported when ChromeOS is using Floss instead of Bluez.
//
// Fetches Bluetooth info and passes the result to the callback. Returns a
// structure with either the Bluetooth information or the error that occurred
// fetching the information.
using FetchBluetoothInfoFromFlossCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::BluetoothResultPtr)>;
void FetchBluetoothInfoFromFloss(Context* context,
                                 FetchBluetoothInfoFromFlossCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_FLOSS_H_
