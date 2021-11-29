// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_

#include <string>

#include <base/callback_forward.h>
#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

class SystemFetcher final : public BaseFetcher {
 public:
  using FetchSystemInfoCallback =
      base::OnceCallback<void(chromeos::cros_healthd::mojom::SystemResultPtr)>;
  using FetchSystemInfoV2Callback = base::OnceCallback<void(
      chromeos::cros_healthd::mojom::SystemResultV2Ptr)>;

  using BaseFetcher::BaseFetcher;

  // DEPRECATED(b/188508561): Use SystemInfoV2 instead.
  void FetchSystemInfo(FetchSystemInfoCallback callback);
  // Returns either a structure with the system information or the error that
  // occurred fetching the information.
  void FetchSystemInfoV2(FetchSystemInfoV2Callback callback);
  // Converts SystemInfoV2 to SystemInfo
  static chromeos::cros_healthd::mojom::SystemInfoPtr ConvertToSystemInfo(
      const chromeos::cros_healthd::mojom::SystemInfoV2Ptr& system_info_v2);

 private:
  bool FetchOsInfoWithoutBootMode(
      chromeos::cros_healthd::mojom::OsInfoPtr* out_os_info,
      chromeos::cros_healthd::mojom::ProbeErrorPtr* out_error);

  void FetchBootMode(
      chromeos::cros_healthd::mojom::SystemInfoV2Ptr system_info_v2,
      const base::FilePath& root_dir,
      FetchSystemInfoV2Callback callback);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_H_
