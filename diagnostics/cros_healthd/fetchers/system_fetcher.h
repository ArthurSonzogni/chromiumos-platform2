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
  chromeos::cros_healthd::mojom::SystemResultV2Ptr FetchSystemInfoV2();
  // DEPRECATED(b/188508561): Use SystemInfoV2 instead.
  chromeos::cros_healthd::mojom::SystemResultPtr FetchSystemInfo();
  // Converts SystemInfoV2 to SystemInfo
  static chromeos::cros_healthd::mojom::SystemInfoPtr ConvertToSystemInfo(
      const chromeos::cros_healthd::mojom::SystemInfoV2Ptr& system_info_v2);

 private:
  bool FetchOsInfo(chromeos::cros_healthd::mojom::OsInfoPtr* out_os_info,
                   chromeos::cros_healthd::mojom::ProbeErrorPtr* out_error);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
