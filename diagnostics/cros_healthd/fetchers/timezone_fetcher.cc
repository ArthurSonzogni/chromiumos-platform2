// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/timezone_fetcher.h"

#include <string>

#include <base/files/file_util.h>
#include <brillo/timezone/tzif_parser.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kLocaltimeFile[] = "var/lib/timezone/localtime";
constexpr char kZoneInfoPath[] = "usr/share/zoneinfo";

}  // namespace

mojom::TimezoneResultPtr FetchTimezoneInfo() {
  base::FilePath timezone_path;
  base::FilePath localtime_path = GetRootDir().AppendASCII(kLocaltimeFile);
  if (!base::NormalizeFilePath(localtime_path, &timezone_path)) {
    return mojom::TimezoneResult::NewError(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        "Unable to read symlink of localtime file: " + localtime_path.value()));
  }

  base::FilePath timezone_region_path;
  base::FilePath zone_info_path = GetRootDir().AppendASCII(kZoneInfoPath);
  if (!zone_info_path.AppendRelativePath(timezone_path,
                                         &timezone_region_path)) {
    return mojom::TimezoneResult::NewError(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        "Unable to get timezone region from zone info path: " +
            timezone_path.value()));
  }
  auto posix_result = brillo::timezone::GetPosixTimezone(timezone_path);
  if (!posix_result) {
    return mojom::TimezoneResult::NewError(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        "Unable to get posix timezone from timezone path: " +
            timezone_path.value()));
  }

  return mojom::TimezoneResult::NewTimezoneInfo(mojom::TimezoneInfo::New(
      posix_result.value(), timezone_region_path.value()));
}

}  // namespace diagnostics
