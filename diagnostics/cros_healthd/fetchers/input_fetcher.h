// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_INPUT_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_INPUT_FETCHER_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

// Fetches the input related information and pass the result to the callback.
using FetchInputInfoCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::InputResultPtr)>;
void FetchInputInfo(Context* context, FetchInputInfoCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_INPUT_FETCHER_H_
