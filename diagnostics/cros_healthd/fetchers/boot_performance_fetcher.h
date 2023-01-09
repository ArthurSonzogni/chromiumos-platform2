// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace path {

inline constexpr char kProcUptime[] = "/proc/uptime";
inline constexpr char kBiosTimes[] = "/var/log/bios_times.txt";
inline constexpr char kShutdownMetrics[] = "/var/log/metrics";
inline constexpr char kPreviousPowerdLog[] =
    "/var/log/power_manager/powerd.PREVIOUS";

}  // namespace path

// The BootPerformanceFetcher class is responsible for gathering boot
// performance info.
class BootPerformanceFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns a structure with either the device's boot performance info or the
  // error that occurred fetching the information.
  ash::cros_healthd::mojom::BootPerformanceResultPtr FetchBootPerformanceInfo();

 private:
  using OptionalProbeErrorPtr =
      std::optional<ash::cros_healthd::mojom::ProbeErrorPtr>;

  OptionalProbeErrorPtr PopulateBootUpInfo(
      ash::cros_healthd::mojom::BootPerformanceInfo* info);

  void PopulateShutdownInfo(
      ash::cros_healthd::mojom::BootPerformanceInfo* info);

  OptionalProbeErrorPtr ParseBootFirmwareTime(double* firmware_time);

  OptionalProbeErrorPtr ParseBootKernelTime(double* kernel_time);

  OptionalProbeErrorPtr ParseProcUptime(double* proc_uptime);

  bool ParsePreviousPowerdLog(double* shutdown_start_timestamp,
                              std::string* shutdown_reason);

  bool GetShutdownEndTimestamp(double* shutdown_end_timestamp);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_
