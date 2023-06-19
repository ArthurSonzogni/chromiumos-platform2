// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

constexpr auto kStatefulPartitionPath = "mnt/stateful_partition";
constexpr auto kMtabPath = "etc/mtab";

// Fetch stateful partition info and pass the result to the callback.
using FetchStatefulPartitionInfoCallback = base::OnceCallback<void(
    ash::cros_healthd::mojom::StatefulPartitionResultPtr)>;
void FetchStatefulPartitionInfo(Context* context,
                                FetchStatefulPartitionInfoCallback callback);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_
