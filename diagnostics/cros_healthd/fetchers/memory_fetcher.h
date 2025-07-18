// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MEMORY_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MEMORY_FETCHER_H_

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class Context;

// Guest VM memory information used for computing the adjusted available
// memory of the VM.
struct GuestMemoryInfo {
  int64_t balloon_size = 0;
  int64_t allocated_memory = 0;
  int64_t available_memory = 0;
  int64_t free_memory = 0;
  int64_t crosvm_rss = 0;
  int64_t crosvm_swap = 0;
};

// Returns a structure with either the device's memory info or the error that
// occurred fetching the information.
using FetchMemoryInfoCallback =
    base::OnceCallback<void(ash::cros_healthd::mojom::MemoryResultPtr)>;
void FetchMemoryInfo(Context* context, FetchMemoryInfoCallback callback);

// Computes the adjusted available memory of the guest VM.
int64_t ComputeAdjustedAvailable(const GuestMemoryInfo& guest);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MEMORY_FETCHER_H_
