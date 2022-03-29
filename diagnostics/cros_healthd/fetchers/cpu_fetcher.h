// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_

#include <base/files/file_path.h>
#include <string>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Directory containing SoC ID info.
inline constexpr char kRelativeSoCDevicesDir[] = "sys/bus/soc/devices/";
// File containing Arm device tree compatible string.
inline constexpr char kRelativeCompatibleFile[] =
    "sys/firmware/devicetree/base/compatible";

// File read from the CPU directory.
inline constexpr char kPresentFileName[] = "present";
// Files read from the C-state directory.
inline constexpr char kCStateNameFileName[] = "name";
inline constexpr char kCStateTimeFileName[] = "time";
// Files read from the CPU policy directory.
inline constexpr char kScalingMaxFreqFileName[] = "scaling_max_freq";
inline constexpr char kScalingCurFreqFileName[] = "scaling_cur_freq";
inline constexpr char kCpuinfoMaxFreqFileName[] = "cpuinfo_max_freq";
// File to read Keylocker information.
inline constexpr char kRelativeCryptoFilePath[] = "proc/crypto";

// Relative path from root of the CPU directory.
inline constexpr char kRelativeCpuDir[] = "sys/devices/system/cpu";

// Returns an absolute path to the C-state directory for the logical CPU with ID
// |logical_id|. On a real device, this will be
// /sys/devices/system/cpu/cpu|logical_id|/cpuidle.
base::FilePath GetCStateDirectoryPath(const base::FilePath& root_dir,
                                      const std::string& logical_id);

// Returns an absolute path to the CPU freq directory for the logical CPU with
// ID |logical_id|. On a real device, this will be
// /sys/devices/system/cpu/cpufreq/policy|logical_id| if the CPU has a governing
// policy, or /sys/devices/system/cpu/|logical_id|/cpufreq without.
base::FilePath GetCpuFreqDirectoryPath(const base::FilePath& root_dir,
                                       const std::string& logical_id);

// The CpuFetcher class is responsible for gathering CPU info reported by
// cros_healthd.
class CpuFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns a structure with a list of data fields for each of the device's
  // CPUs or the error that occurred fetching the information.
  chromeos::cros_healthd::mojom::CpuResultPtr FetchCpuInfo();

 private:
  // Uses |context_| to obtain the CPU architecture.
  chromeos::cros_healthd::mojom::CpuArchitectureEnum GetArchitecture();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
