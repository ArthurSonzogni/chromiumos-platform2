// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_

#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

class SystemFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns either a structure with the system information or the error that
  // occurred fetching the information.
  chromeos::cros_healthd::mojom::SystemResultPtr FetchSystemInfo();

 private:
  // Fetches information from the master configuration using CrosConfig. Since
  // this function does not read from a file, it does not check for errors.
  void FetchMasterConfigInfo(
      chromeos::cros_healthd::mojom::SystemInfo* output_info);

  // Fetches the operating system version and populates the |output_info|
  // structure.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr> FetchOsVersion(
      chromeos::cros_healthd::mojom::OsVersion* os_version);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
