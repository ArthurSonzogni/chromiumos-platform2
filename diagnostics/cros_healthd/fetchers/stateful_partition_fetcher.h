// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

constexpr auto kStatefulPartitionPath = "mnt/stateful_partition";
constexpr auto kMtabPath = "etc/mtab";

// The StatefulPartitionFetcher class is responsible for gathering stateful
// partition info.
class StatefulPartitionFetcher final {
 public:
  explicit StatefulPartitionFetcher(Context* context);
  StatefulPartitionFetcher(const StatefulPartitionFetcher&) = delete;
  StatefulPartitionFetcher& operator=(const StatefulPartitionFetcher&) = delete;
  ~StatefulPartitionFetcher() = default;

  // Returns stateful partition data or the error
  // that occurred retrieving the information.
  chromeos::cros_healthd::mojom::StatefulPartitionResultPtr
  FetchStatefulPartitionInfo();

 private:
  // Unowned pointer that outlives this StatefulPartitionFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_
