// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <vector>

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

// These functions are used for gathering information about a particular,
// multiple or all processes on the device.

// Returns information about a particular process on the device, or the error
// that occurred retrieving the information. |process_id| is the PID for the
// process whose information will be fetched.
void FetchProcessInfo(
    Context* context,
    uint32_t process_id,
    base::OnceCallback<void(ash::cros_healthd::mojom::ProcessResultPtr)>
        callback);

// Returns information about multiple specified or all processes on the
// device, and the errors if any occurred and not ignored when retrieving the
// information. |input_process_ids| is the array of PIDs for the processes
// whose information will be fetched. |ignore_single_process_error| will
// enable errors to be ignored when fetching process infos if set to true.
void FetchMultipleProcessInfo(
    Context* context,
    const std::optional<std::vector<uint32_t>>& input_process_ids,
    const bool ignore_single_process_error,
    base::OnceCallback<void(ash::cros_healthd::mojom::MultipleProcessResultPtr)>
        callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_
