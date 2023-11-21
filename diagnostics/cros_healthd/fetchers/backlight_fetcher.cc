// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/backlight_fetcher.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/types/expected.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kRelativeBacklightDirectoryPath[] = "sys/class/backlight";

// Fetches backlight information for a specific sysfs path. On success,
// returns fetched information. When an error occurs,returns a unexpected
// ProbeError.
base::expected<mojom::BacklightInfoPtr, mojom::ProbeErrorPtr>
FetchBacklightInfoForPath(const base::FilePath& path) {
  auto info = mojom::BacklightInfo::New();
  info->path = path.value();

  if (!ReadInteger(path, "max_brightness", &base::StringToUint,
                   &info->max_brightness)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        "Failed to read max_brightness for " + path.value()));
  }

  if (!ReadInteger(path, "brightness", &base::StringToUint,
                   &info->brightness)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        "Failed to read brightness for " + path.value()));
  }

  return base::ok(std::move(info));
}

}  // namespace

mojom::BacklightResultPtr FetchBacklightInfo(Context* context) {
  std::vector<mojom::BacklightInfoPtr> backlights;

  if (!context->system_config()->HasBacklight()) {
    return mojom::BacklightResult::NewBacklightInfo(std::move(backlights));
  }

  base::FileEnumerator backlight_dirs(
      GetRootDir().AppendASCII(kRelativeBacklightDirectoryPath),
      false /* is_recursive */,
      base::FileEnumerator::SHOW_SYM_LINKS | base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES);

  for (base::FilePath path = backlight_dirs.Next(); !path.empty();
       path = backlight_dirs.Next()) {
    VLOG(1) << "Processing the node " << path.value();
    auto result = FetchBacklightInfoForPath(path);
    if (!result.has_value()) {
      return mojom::BacklightResult::NewError(std::move(result.error()));
    }
    DCHECK_NE(result.value()->path, "");
    DCHECK_LE(result.value()->brightness, result.value()->max_brightness);
    backlights.push_back(std::move(result.value()));
  }

  if (backlights.empty()) {
    return mojom::BacklightResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kFileReadError,
                               "Device supports backlight, but no backlight "
                               "information found in /sys/class/backlight"));
  }

  return mojom::BacklightResult::NewBacklightInfo(std::move(backlights));
}

}  // namespace diagnostics
