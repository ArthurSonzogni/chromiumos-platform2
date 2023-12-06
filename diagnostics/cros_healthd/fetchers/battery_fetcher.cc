// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/system/powerd_adapter.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Mapping from device model to i2c port number.
constexpr auto kModelToPort =
    base::MakeFixedFlatMap<std::string_view, uint8_t>({
        {"sona", 2},
        {"careena", 0},
        {"dratini", 5},
        {"drobit", 5},
        {"dorp", 0},
        {"frostflow", 2},
        {"marasov", 5},
        {"starmie", 1},
        {"chinchou", 1},
        {"chinchou360", 1},
    });

// Converts a Smart Battery manufacture date from the ((year - 1980) * 512 +
// month * 32 + day) format to yyyy-mm-dd format.
std::string ConvertSmartBatteryManufactureDate(uint32_t manufacture_date) {
  int remainder = manufacture_date;
  int day = remainder % 32;
  remainder /= 32;
  int month = remainder % 16;
  remainder /= 16;
  int year = remainder + 1980;
  return base::StringPrintf("%04d-%02d-%02d", year, month, day);
}

base::expected<mojom::BatteryInfoPtr, mojom::ProbeErrorPtr>
PopulateBatteryInfoFromPowerdResponse(Context* context) {
  auto power_supply_proto =
      context->powerd_adapter()->GetPowerSupplyProperties();
  if (!power_supply_proto.has_value()) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        "Failed to obtain power supply properties from powerd"));
  }

  if (!power_supply_proto->has_battery_state() ||
      power_supply_proto->battery_state() ==
          power_manager::PowerSupplyProperties_BatteryState_NOT_PRESENT) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        "PowerSupplyProperties protobuf indicates battery is not present"));
  }

  auto info = mojom::BatteryInfo::New();

  info->cycle_count = power_supply_proto->battery_cycle_count();
  info->vendor = power_supply_proto->battery_vendor();
  info->voltage_now = power_supply_proto->battery_voltage();
  info->charge_full = power_supply_proto->battery_charge_full();
  info->charge_full_design = power_supply_proto->battery_charge_full_design();
  info->serial_number = power_supply_proto->battery_serial_number();
  info->voltage_min_design = power_supply_proto->battery_voltage_min_design();
  info->model_name = power_supply_proto->battery_model_name();
  info->charge_now = power_supply_proto->battery_charge();
  info->current_now = power_supply_proto->battery_current();
  info->technology = power_supply_proto->battery_technology();
  info->status = power_supply_proto->battery_status();

  return base::ok(std::move(info));
}

class State {
 public:
  explicit State(mojom::BatteryInfoPtr info);
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() = default;

  // Handle the response of manufacture date from the executor.
  void HandleManufactureDateResponse(std::optional<uint32_t> manufacture_date);

  // Handle the response of temperature from the executor.
  void HandleTemperatureResponse(std::optional<uint32_t> temperature);

  // Send back the BatteryResult via |callback|. The result is ProbeError if
  // |error_| is not null or |is_finished| is false, otherwise |info_|.
  void HandleResult(FetchBatteryInfoCallback callback, bool is_finished);

 private:
  // The info to be returned.
  mojom::BatteryInfoPtr info_;
  // The error to be returned.
  mojom::ProbeErrorPtr error_;
};

State::State(mojom::BatteryInfoPtr info) : info_(std::move(info)) {}

void State::HandleManufactureDateResponse(
    std::optional<uint32_t> manufacture_date) {
  if (!manufacture_date.has_value()) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get manufacture date.");
    return;
  }
  info_->manufacture_date =
      ConvertSmartBatteryManufactureDate(manufacture_date.value());
}

void State::HandleTemperatureResponse(std::optional<uint32_t> temperature) {
  if (!temperature.has_value()) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to get manufacture date.");
    return;
  }
  info_->temperature = mojom::NullableUint64::New(temperature.value());
}

void State::HandleResult(FetchBatteryInfoCallback callback, bool is_finished) {
  if (!is_finished) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to finish all callbacks.");
  }
  if (!error_.is_null()) {
    std::move(callback).Run(mojom::BatteryResult::NewError(std::move(error_)));
    return;
  }
  std::move(callback).Run(
      mojom::BatteryResult::NewBatteryInfo(std::move(info_)));
}

}  // namespace

void FetchBatteryInfo(Context* context, FetchBatteryInfoCallback callback) {
  if (!context->system_config()->HasBattery()) {
    std::move(callback).Run(mojom::BatteryResult::NewBatteryInfo(nullptr));
    return;
  }
  auto info = PopulateBatteryInfoFromPowerdResponse(context);
  if (!info.has_value()) {
    std::move(callback).Run(
        mojom::BatteryResult::NewError(std::move(info.error())));
    return;
  }

  // Check if the device has smart battery via cros config.
  if (!context->system_config()->HasSmartBattery()) {
    std::move(callback).Run(
        mojom::BatteryResult::NewBatteryInfo(std::move(info.value())));
    return;
  }

  // Device with smart battery should have a Google EC.
  if (!context->ground_truth()->HasCrosEC()) {
    std::move(callback).Run(mojom::BatteryResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                               "Failed to find EC for smart battery info.")));
    return;
  }

  auto model_name = context->system_config()->GetCodeName();
  auto it = kModelToPort.find(model_name);
  if (it == kModelToPort.end()) {
    LOG(ERROR) << "Failed to get i2c port for model: " << model_name;
    std::move(callback).Run(
        mojom::BatteryResult::NewError(CreateAndLogProbeError(
            mojom::ErrorType::kSystemUtilityError, "Failed to get i2c port.")));
    return;
  }
  auto i2c_port = it->second;

  auto state = std::make_unique<State>(std::move(info.value()));
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::HandleResult, std::move(state),
                                         std::move(callback))};

  context->executor()->GetSmartBatteryManufactureDate(
      i2c_port,
      barrier.Depend(base::BindOnce(&State::HandleManufactureDateResponse,
                                    base::Unretained(state_ptr))));
  context->executor()->GetSmartBatteryTemperature(
      i2c_port, barrier.Depend(base::BindOnce(&State::HandleTemperatureResponse,
                                              base::Unretained(state_ptr))));
}

}  // namespace diagnostics
