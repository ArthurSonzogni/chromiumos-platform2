// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/sensor_fetcher.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {
namespace {

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";

std::pair<mojom::NullableUint16Ptr, mojom::ProbeErrorPtr> ParseLidAngle(
    std::string input) {
  // Format of |input|: "Lid angle: ${LID_ANGLE}\n"
  std::vector<std::string> tokens =
      base::SplitString(input.substr(0, input.find_last_of("\n")), ":",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (tokens.size() == 2 && tokens[0] == "Lid angle") {
    uint lid_angle;
    if (base::StringToUint(tokens[1], &lid_angle))
      return {mojom::NullableUint16::New(lid_angle), nullptr};
    else if (tokens[1] == "unreliable")
      return {nullptr, nullptr};
  }

  return {nullptr, CreateAndLogProbeError(
                       mojom::ErrorType::kParseError,
                       base::StringPrintf(
                           "GetLidAngle output is incorrectly formatted: %s",
                           input.c_str()))};
}

void HandleLidAngleResponse(Context* context,
                            FetchSensorInfoCallback callback,
                            mojom::ExecutedProcessResultPtr result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;
  if (!err.empty() || return_code != EXIT_SUCCESS) {
    std::move(callback).Run(
        mojom::SensorResult::NewError(CreateAndLogProbeError(
            mojom::ErrorType::kSystemUtilityError,
            base::StringPrintf(
                "GetLidAngle failed with return code: %d and error: %s",
                return_code, err.c_str()))));
    return;
  }

  mojom::NullableUint16Ptr lid_angle;
  mojom::ProbeErrorPtr error;
  std::tie(lid_angle, error) = ParseLidAngle(result->out);
  if (!error.is_null()) {
    std::move(callback).Run(mojom::SensorResult::NewError(std::move(error)));
    return;
  }

  std::move(callback).Run(mojom::SensorResult::NewSensorInfo(
      mojom::SensorInfo::New(std::move(lid_angle))));
}

}  // namespace

void FetchSensorInfo(Context* context, FetchSensorInfoCallback callback) {
  // Devices without a Google EC, and therefore ectool, cannot obtain lid angle.
  if (!base::PathExists(context->root_dir().Append(kRelativeCrosEcPath))) {
    LOG(INFO) << "Device does not have a Google EC.";
    std::move(callback).Run(
        mojom::SensorResult::NewSensorInfo(mojom::SensorInfo::New()));
    return;
  }

  // Get lid angle.
  context->executor()->GetLidAngle(
      base::BindOnce(&HandleLidAngleResponse, context, std::move(callback)));
}

}  // namespace diagnostics
