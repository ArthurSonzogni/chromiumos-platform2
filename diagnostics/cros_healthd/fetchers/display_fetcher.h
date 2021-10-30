// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISPLAY_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISPLAY_FETCHER_H_

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_executor.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The DisplayFetcher class is responsible for gathering display info reported
// by cros_healthd. Info is fetched via |modetest|.
class DisplayFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  using FetchDisplayInfoCallback =
      base::OnceCallback<void(chromeos::cros_healthd::mojom::DisplayResultPtr)>;
  // Returns a structure with either the device's display info or the error that
  // occurred fetching the information.
  void FetchDisplayInfo(FetchDisplayInfoCallback&& callback);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISPLAY_FETCHER_H_
