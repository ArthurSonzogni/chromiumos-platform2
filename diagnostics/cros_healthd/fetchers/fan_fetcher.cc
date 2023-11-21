// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

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

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void HandleFanSpeedResponse(FetchFanInfoCallback callback,
                            const std::vector<uint16_t>& fan_rpms,
                            const std::optional<std::string>& error) {
  if (error.has_value()) {
    std::move(callback).Run(mojom::FanResult::NewError(CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        base::StringPrintf("GetAllFanSpeed failed with error: %s",
                           error.value().c_str()))));
    return;
  }

  std::vector<mojom::FanInfoPtr> fan_info;
  for (uint16_t fan_rpm : fan_rpms) {
    // The healthd interface for fan telemetry returns uint32 as the fan rpm
    // value. Typecast into uint32 here.
    fan_info.push_back(mojom::FanInfo::New(static_cast<uint32_t>(fan_rpm)));
  }

  std::move(callback).Run(mojom::FanResult::NewFanInfo(std::move(fan_info)));
}

}  // namespace

void FetchFanInfo(Context* context, FetchFanInfoCallback callback) {
  // Devices without a Google EC, and therefore ectool, cannot obtain fan info.
  if (!context->ground_truth()->HasCrosEC()) {
    LOG(INFO) << "Device does not have a Google EC.";
    std::move(callback).Run(mojom::FanResult::NewFanInfo({}));
    return;
  }

  context->executor()->GetAllFanSpeed(
      base::BindOnce(&HandleFanSpeedResponse, std::move(callback)));
}

}  // namespace diagnostics
