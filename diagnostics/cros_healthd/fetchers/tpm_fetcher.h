// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TPM_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TPM_FETCHER_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

inline constexpr auto kFileTpmDidVid = "sys/class/tpm/tpm0/did_vid";

// Returns a structure with either the device's tpm data or the error
// that occurred fetching the information.
using FetchTpmInfoCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::TpmResultPtr)>;
void FetchTpmInfo(Context* context, FetchTpmInfoCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TPM_FETCHER_H_
