// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

void FanFetcher::FetchFanInfo(FetchFanInfoCallback callback) {
  // Devices without a Google EC, and therefore ectool, cannot obtain fan info.
  if (!base::PathExists(context_->root_dir().Append(kRelativeCrosEcPath))) {
    LOG(INFO) << "Device does not have a Google EC.";
    std::move(callback).Run(mojom::FanResult::NewFanInfo({}));
    return;
  }

  context_->executor()->GetAllFanSpeed(
      base::BindOnce(&FanFetcher::HandleFanSpeedResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void FanFetcher::HandleFanSpeedResponse(
    FetchFanInfoCallback callback,
    const std::vector<uint32_t>& fan_rpms,
    const std::optional<std::string>& error) {
  if (error.has_value()) {
    std::move(callback).Run(mojom::FanResult::NewError(CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        base::StringPrintf("GetAllFanSpeed failed with error: %s",
                           error.value().c_str()))));
    return;
  }

  std::vector<mojom::FanInfoPtr> fan_info;
  for (uint32_t fan_rpm : fan_rpms) {
    fan_info.push_back(mojom::FanInfo::New(fan_rpm));
  }

  std::move(callback).Run(mojom::FanResult::NewFanInfo(std::move(fan_info)));
}

}  // namespace diagnostics
