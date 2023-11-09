// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <debugd/dbus-proxies.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// The name of the Smart Battery manufacture date metric.
constexpr char kManufactureDateSmart[] = "manufacture_date_smart";
// The name of the Smart Battery temperature metric.
constexpr char kTemperatureSmart[] = "temperature_smart";

// The maximum amount of time to wait for a debugd response.
constexpr int kDebugdDBusTimeout = 10 * 1000;

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

std::optional<mojom::ProbeErrorPtr> PopulateBatteryInfoFromPowerdResponse(
    Context* context, const mojom::BatteryInfoPtr& info) {
  auto power_supply_proto =
      context->powerd_adapter()->GetPowerSupplyProperties();
  if (!power_supply_proto.has_value()) {
    return CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        "Failed to obtain power supply properties from powerd");
  }

  if (!power_supply_proto->has_battery_state() ||
      power_supply_proto->battery_state() ==
          power_manager::PowerSupplyProperties_BatteryState_NOT_PRESENT) {
    return CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        "PowerSupplyProperties protobuf indicates battery is not present");
  }

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

  return std::nullopt;
}

template <typename T>
std::optional<mojom::ProbeErrorPtr> GetSmartBatteryMetric(
    Context* context,
    const std::string& metric_name,
    base::OnceCallback<bool(std::string_view input, T* output)>
        convert_string_to_num,
    T* metric_value) {
  brillo::ErrorPtr error;
  std::string debugd_result;
  if (!context->debugd_proxy()->CollectSmartBatteryMetric(
          metric_name, &debugd_result, &error, kDebugdDBusTimeout)) {
    return CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                  "Failed retrieving " + metric_name +
                                      " from debugd: " + error->GetCode() +
                                      " " + error->GetMessage());
  }

  // Parse the output from debugd to obtain the battery metric.
  constexpr auto kRegexPattern =
      R"(^Read from I2C port [\d]+ at .* offset .* = (.+)$)";
  std::string reg_value;
  if (!RE2::PartialMatch(base::CollapseWhitespaceASCII(debugd_result, true),
                         kRegexPattern, &reg_value)) {
    return CreateAndLogProbeError(
        mojom::ErrorType::kParseError,
        "Failed to match debugd output to regex: " + debugd_result);
  }

  if (!std::move(convert_string_to_num).Run(reg_value, metric_value)) {
    return CreateAndLogProbeError(
        mojom::ErrorType::kParseError,
        "Unable to run convert string to num callback");
  }

  return std::nullopt;
}

std::optional<mojom::ProbeErrorPtr> PopulateSmartBatteryInfo(
    Context* context, const mojom::BatteryInfoPtr& info) {
  uint32_t manufacture_date;
  auto error = GetSmartBatteryMetric(context, kManufactureDateSmart,
                                     base::BindOnce(&base::HexStringToUInt),
                                     &manufacture_date);
  if (error.has_value()) {
    return error;
  }
  info->manufacture_date = ConvertSmartBatteryManufactureDate(manufacture_date);

  uint64_t temperature;
  error = GetSmartBatteryMetric(context, kTemperatureSmart,
                                base::BindOnce(&base::HexStringToUInt64),
                                &temperature);
  if (error.has_value()) {
    return error;
  }
  info->temperature = mojom::NullableUint64::New(temperature);

  return std::nullopt;
}

}  // namespace

mojom::BatteryResultPtr FetchBatteryInfo(Context* context) {
  if (!context->system_config()->HasBattery())
    return mojom::BatteryResult::NewBatteryInfo(mojom::BatteryInfoPtr());

  auto info = mojom::BatteryInfo::New();
  auto error = PopulateBatteryInfoFromPowerdResponse(context, info);
  if (error.has_value()) {
    return mojom::BatteryResult::NewError(std::move(error.value()));
  }

  if (context->system_config()->HasSmartBattery()) {
    error = PopulateSmartBatteryInfo(context, info);
    if (error.has_value()) {
      return mojom::BatteryResult::NewError(std::move(error.value()));
    }
  }

  return mojom::BatteryResult::NewBatteryInfo(std::move(info));
}

}  // namespace diagnostics
