// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_GRAPHICS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_GRAPHICS_FETCHER_H_

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The GraphicsFetcher class is responsible for gathering graphics info reported
// by cros_healthd.
class GraphicsFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns a structure with either the device's graphics info or the error
  // that occurred fetching the information.
  chromeos::cros_healthd::mojom::GraphicsResultPtr FetchGraphicsInfo();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_GRAPHICS_FETCHER_H_
