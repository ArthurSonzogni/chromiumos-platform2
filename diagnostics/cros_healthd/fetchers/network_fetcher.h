// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_FETCHER_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

class Context;

// Fetch network information and pass the result to the callback.
using FetchNetworkInfoCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::NetworkResultPtr)>;

void FetchNetworkInfo(Context* context, FetchNetworkInfoCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_FETCHER_H_
