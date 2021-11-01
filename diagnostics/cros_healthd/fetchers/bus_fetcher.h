// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The BusFetcher class is responsible for gathering Bus info reported by
// cros_healthd.
class BusFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns a structure with a list of data fields for each of the bus device
  // or the error that occurred fetching the information.
  chromeos::cros_healthd::mojom::BusResultPtr FetchBusDevices();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BUS_FETCHER_H_
