// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/sensor_fetcher.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <iioservice/mojo/sensor.mojom.h>

#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {
namespace {

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";

// Acceptable error code for getting lid angle.
constexpr int kInvalidCommandCode = 1;
constexpr int kInvalidParamCode = 3;

// Parse the raw lid angle and return ProbeError on failure.
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

// Filter for the sensor type we want and convert it to mojom::Sensor::Type.
std::vector<mojom::Sensor::Type> GetSupportedTypes(
    const std::vector<cros::mojom::DeviceType>& types) {
  std::vector<mojom::Sensor::Type> out_types;

  for (const auto& type : types) {
    switch (type) {
      case cros::mojom::DeviceType::ACCEL:
        out_types.push_back(mojom::Sensor::Type::kAccel);
        break;
      case cros::mojom::DeviceType::LIGHT:
        out_types.push_back(mojom::Sensor::Type::kLight);
        break;
      case cros::mojom::DeviceType::ANGLVEL:
        out_types.push_back(mojom::Sensor::Type::kGyro);
        break;
      case cros::mojom::DeviceType::ANGL:
        out_types.push_back(mojom::Sensor::Type::kAngle);
        break;
      case cros::mojom::DeviceType::GRAVITY:
        out_types.push_back(mojom::Sensor::Type::kGravity);
        break;
      default:
        // Ignore other sensor types.
        LOG(ERROR) << "Unsupport sensor device type: " << type;
        break;
    }
  }
  return out_types;
}

class State {
 public:
  State();
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() = default;

  // Handle the response of sensor id and types from the sensor service.
  void HandleSensorIdsTypesResponse(
      const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
          ids_types);

  // Handle the response of lid angle from the executor.
  void HandleLidAngleResponse(mojom::ExecutedProcessResultPtr result);

  // Send back the SensorResult via |callback|. The result is ProbeError if
  // |error_| is not null or |is_finished| is false, otherwise |info_|.
  void HandleResult(FetchSensorInfoCallback callback, bool is_finished);

 private:
  // The info to be returned.
  mojom::SensorInfoPtr info_;
  // The error to be returned.
  mojom::ProbeErrorPtr error_;
};

State::State() : info_(mojom::SensorInfo::New()) {
  info_->sensors = std::vector<mojom::SensorPtr>{};
}

void State::HandleSensorIdsTypesResponse(
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        ids_types) {
  for (const auto& id_types : ids_types) {
    auto types = GetSupportedTypes(id_types.second);
    for (const auto& type : types) {
      info_->sensors->push_back(
          mojom::Sensor::New(std::nullopt, id_types.first, type,
                             mojom::Sensor::Location::kUnknown));
    }
  }
}

void State::HandleLidAngleResponse(mojom::ExecutedProcessResultPtr result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;

  // Some devices don't support `ectool motionsense lid_angle` and will return
  // two return codes, INVALID_COMMAND and INVALID_PARAM, which are acceptable.
  if (return_code == kInvalidCommandCode || return_code == kInvalidParamCode) {
    return;
  }

  if (!err.empty() || return_code != EXIT_SUCCESS) {
    error_ = CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        base::StringPrintf(
            "GetLidAngle failed with return code: %d and error: %s",
            return_code, err.c_str()));
    return;
  }

  mojom::ProbeErrorPtr parse_error;
  std::tie(info_->lid_angle, parse_error) = ParseLidAngle(result->out);
  if (!parse_error.is_null())
    error_ = std::move(parse_error);
}

void State::HandleResult(FetchSensorInfoCallback callback, bool is_finished) {
  if (!is_finished) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to finish all callbacks.");
  }

  if (!error_.is_null()) {
    std::move(callback).Run(mojom::SensorResult::NewError(std::move(error_)));
    return;
  }
  std::move(callback).Run(mojom::SensorResult::NewSensorInfo(std::move(info_)));
}

}  // namespace

void FetchSensorInfo(Context* context, FetchSensorInfoCallback callback) {
  auto state = std::make_unique<State>();
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::HandleResult, std::move(state),
                                         std::move(callback))};

  // Get sensors' attributes.
  context->mojo_service()->GetSensorService()->GetAllDeviceIds(
      barrier.Depend(base::BindOnce(&State::HandleSensorIdsTypesResponse,
                                    base::Unretained(state_ptr))));

  // Devices without a Google EC, and therefore ectool, cannot obtain lid angle.
  if (base::PathExists(context->root_dir().Append(kRelativeCrosEcPath))) {
    context->executor()->GetLidAngle(barrier.Depend(base::BindOnce(
        &State::HandleLidAngleResponse, base::Unretained(state_ptr))));
  }
}

}  // namespace diagnostics
