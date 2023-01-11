// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_

#include <base/files/file_path.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace path {

inline constexpr char kProcUptime[] = "/proc/uptime";
inline constexpr char kBiosTimes[] = "/var/log/bios_times.txt";
inline constexpr char kShutdownMetrics[] = "/var/log/metrics";
inline constexpr char kPreviousPowerdLog[] =
    "/var/log/power_manager/powerd.PREVIOUS";

}  // namespace path

// Returns a structure with either the device's boot performance info or the
// error that occurred fetching the information.
ash::cros_healthd::mojom::BootPerformanceResultPtr FetchBootPerformanceInfo();

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_
