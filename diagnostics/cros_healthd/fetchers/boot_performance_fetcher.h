// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Relative path to boot up information.
extern const char kRelativeBiosTimesPath[];
extern const char kRelativeUptimeLoginPath[];

// Relative path to shutdown information.
extern const char kRelativeShutdownMetricsPath[];
extern const char kRelativePreviousPowerdLogPath[];

// The BootPerformanceFetcher class is responsible for gathering boot
// performance info.
class BootPerformanceFetcher final {
 public:
  explicit BootPerformanceFetcher(Context* context);
  BootPerformanceFetcher(const BootPerformanceFetcher&) = delete;
  BootPerformanceFetcher& operator=(const BootPerformanceFetcher&) = delete;
  ~BootPerformanceFetcher();

  // Returns a structure with either the device's boot performance info or the
  // error that occurred fetching the information.
  chromeos::cros_healthd::mojom::BootPerformanceResultPtr
  FetchBootPerformanceInfo();

 private:
  using OptionalProbeErrorPtr =
      base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>;

  OptionalProbeErrorPtr PopulateBootUpInfo(
      chromeos::cros_healthd::mojom::BootPerformanceInfo* info);

  void PopulateShutdownInfo(
      chromeos::cros_healthd::mojom::BootPerformanceInfo* info);

  OptionalProbeErrorPtr ParseBootFirmwareTime(double* firmware_time);

  OptionalProbeErrorPtr ParseBootKernelTime(double* kernel_time);

  OptionalProbeErrorPtr ParseProcUptime(double* proc_uptime);

  bool ParsePreviousPowerdLog(double* shutdown_start_timestamp,
                              std::string* shutdown_reason);

  bool GetShutdownEndTimestamp(double* shutdown_end_timestamp);

  // Unowned pointer that outlives this BootPerformanceFetcher instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_BOOT_PERFORMANCE_FETCHER_H_
