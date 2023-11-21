// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BACKLIGHT_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BACKLIGHT_FETCHER_H_

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

// Returns a structure with either the device's backlight info or the error that
// occurred fetching the information.
ash::cros_healthd::mojom::BacklightResultPtr FetchBacklightInfo(
    Context* context);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BACKLIGHT_FETCHER_H_
