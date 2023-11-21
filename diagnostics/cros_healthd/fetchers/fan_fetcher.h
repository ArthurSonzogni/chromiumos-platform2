// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

using FetchFanInfoCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::FanResultPtr)>;

// Returns either a list of data about each of the device's fans or the error
// that occurred retrieving the information.
void FetchFanInfo(Context* context, FetchFanInfoCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_FAN_FETCHER_H_
