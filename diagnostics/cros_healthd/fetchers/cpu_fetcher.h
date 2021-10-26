// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Directory containing SoC ID info.
inline constexpr char kRelativeSoCDevicesDir[] = "sys/bus/soc/devices/";

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
