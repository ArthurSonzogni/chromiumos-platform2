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

namespace mojom = ::ash::cros_healthd::mojom;

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";

// Acceptable error code for getting lid angle.
constexpr int kInvalidCommandCode = 1;
constexpr int kInvalidParamCode = 3;

// The target sensor attributes to fetch.
const std::vector<std::string> kTargetSensorAttributes_ = {
    cros::mojom::kDeviceName, cros::mojom::kLocation};

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
      case cros::mojom::DeviceType::MAGN:
        out_types.push_back(mojom::Sensor::Type::kMagn);
        break;
      default:
        // Ignore other sensor types.
        LOG(ERROR) << "Unsupport sensor device type: " << type;
        break;
    }
  }
  return out_types;
}

// Convert the location string to mojom::Sensor::Location.
mojom::Sensor::Location ConvertLocation(
    const std::optional<std::string>& location) {
  if (location.has_value()) {
    if (location.value() == cros::mojom::kLocationBase)
      return mojom::Sensor::Location::kBase;
    else if (location.value() == cros::mojom::kLocationLid)
      return mojom::Sensor::Location::kLid;
    else if (location.value() == cros::mojom::kLocationCamera)
      return mojom::Sensor::Location::kCamera;
  }
  return mojom::Sensor::Location::kUnknown;
}

class State {
 public:
  explicit State(MojoService* mojo_service);
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() = default;

  // Handle the response of sensor id and types from the sensor service.
  void HandleSensorIdsTypesResponse(
      base::OnceClosure completion_callback,
      const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
          ids_types);

  // Handle the response of sensor attributes from the sensor device.
  void HandleAttributesResponse(
      int32_t id,
      const std::vector<mojom::Sensor::Type>& types,
      const std::vector<std::optional<std::string>>& attributes);

  // Handle the response of lid angle from the executor.
  void HandleLidAngleResponse(mojom::ExecutedProcessResultPtr result);

  // Send back the SensorResult via |callback|. The result is ProbeError if
  // |error_| is not null or |is_finished| is false, otherwise |info_|.
  void HandleResult(FetchSensorInfoCallback callback, bool is_finished);

 private:
  // Used to get sensor devices.
  MojoService* const mojo_service_;
  // The info to be returned.
  mojom::SensorInfoPtr info_;
  // The error to be returned.
  mojom::ProbeErrorPtr error_;
};

State::State(MojoService* mojo_service)
    : mojo_service_(mojo_service), info_(mojom::SensorInfo::New()) {
  info_->sensors = std::vector<mojom::SensorPtr>{};
}

void State::HandleSensorIdsTypesResponse(
    base::OnceClosure completion_callback,
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        ids_types) {
  CallbackBarrier barrier{/*on_success=*/std::move(completion_callback),
                          /*on_error=*/base::DoNothing()};
  for (const auto& id_types : ids_types) {
    auto types = GetSupportedTypes(id_types.second);
    if (types.size() == 0)
      continue;

    mojo_service_->GetSensorDevice(id_types.first)
        ->GetAttributes(kTargetSensorAttributes_,
                        barrier.Depend(base::BindOnce(
                            &State::HandleAttributesResponse,
                            base::Unretained(this), id_types.first, types)));
  }
}

void State::HandleAttributesResponse(
    int32_t id,
    const std::vector<mojom::Sensor::Type>& types,
    const std::vector<std::optional<std::string>>& attributes) {
  if (attributes.size() != kTargetSensorAttributes_.size()) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kParseError,
                                    "Failed to get valid sensor attributes.");
    return;
  }
  for (const auto& type : types) {
    info_->sensors->push_back(mojom::Sensor::New(
        attributes[0], id, type, ConvertLocation(attributes[1])));
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
  auto* mojo_service = context->mojo_service();
  auto state = std::make_unique<State>(mojo_service);
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::HandleResult, std::move(state),
                                         std::move(callback))};

  // Get sensors' attributes.
  mojo_service->GetSensorService()->GetAllDeviceIds(
      barrier.Depend(base::BindOnce(&State::HandleSensorIdsTypesResponse,
                                    base::Unretained(state_ptr),
                                    barrier.CreateDependencyClosure())));

  // Devices without a Google EC, and therefore ectool, cannot obtain lid angle.
  if (base::PathExists(context->root_dir().Append(kRelativeCrosEcPath))) {
    context->executor()->GetLidAngle(barrier.Depend(base::BindOnce(
        &State::HandleLidAngleResponse, base::Unretained(state_ptr))));
  }
}

}  // namespace diagnostics
