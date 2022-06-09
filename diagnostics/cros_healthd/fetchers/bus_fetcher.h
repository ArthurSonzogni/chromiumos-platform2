// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_

#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/cros_healthd/utils/fwupd_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = chromeos::cros_healthd::mojom;

// The BusFetcher class is responsible for gathering Bus info reported by
// cros_healthd.
class BusFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  using FetchBusDevicesCallback = base::OnceCallback<void(mojom::BusResultPtr)>;

  // Returns a structure with a list of data fields for each of the bus device
  // or the error that occurred fetching the information.
  void FetchBusDevices(FetchBusDevicesCallback&& callback);

 private:
  void FetchBusDevicesWithFwupdInfo(
      FetchBusDevicesCallback&& callback,
      const fwupd_utils::DeviceList& fwupd_devices);

  // Must be the last member of the class, so that it's destroyed first when an
  // instance of the class is destroyed. This will prevent any outstanding
  // callbacks from being run and segfaulting.
  base::WeakPtrFactory<BusFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_
