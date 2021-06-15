// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The BluetoothFetcher class is responsible for gathering a device's Bluetooth
// information.
class BluetoothFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns the device's Bluetooth information.
  chromeos::cros_healthd::mojom::BluetoothResultPtr FetchBluetoothInfo();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BLUETOOTH_FETCHER_H_
