// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/system_config.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/functional/callback.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>
#include <brillo/errors/error.h>
#include <debugd/dbus-proxies.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/system/cros_config.h"
#include "diagnostics/cros_healthd/system/debugd_constants.h"
#include "diagnostics/cros_healthd/system/system_config_constants.h"

namespace diagnostics {

namespace {

// The field that contains the bitvalue if the NVMe self test is supported by
// the device.
constexpr char kNvmeSelfTestCtrlField[] = "oacs";

// Bitmask for the bit that shows if the device supports the self test feature.
// 4th bit zero index.
constexpr uint8_t kNvmeSelfTestBitmask = 16;

bool NvmeSelfTestSupportedFromIdentity(const std::string& nvmeIdentity) {
  // Example output:
  // oacs      : 0x17
  // acl       : 3
  // aerl      : 7
  // frmw      : 0x16
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(nvmeIdentity, ':', '\n', &pairs);
  for (const auto& [key, value] : pairs) {
    if (base::TrimWhitespaceASCII(key, base::TrimPositions::TRIM_TRAILING) !=
        kNvmeSelfTestCtrlField) {
      continue;
    }

    u_int32_t oacs_value;
    if (!base::HexStringToUInt(
            base::TrimWhitespaceASCII(value, base::TrimPositions::TRIM_ALL),
            &oacs_value)) {
      return false;
    }

    // Check to see if the device-self-test support bit is set
    return ((oacs_value & kNvmeSelfTestBitmask) == kNvmeSelfTestBitmask);
  }

  return false;
}

void NvmeSelfTestSupportedByDebugd(
    org::chromium::debugdProxyInterface* debugd_proxy,
    SystemConfig::NvmeSelfTestSupportedCallback callback) {
  auto [cb1, cb2] = base::SplitOnceCallback(std::move(callback));
  debugd_proxy->NvmeAsync(
      kNvmeIdentityOption,
      base::BindOnce(&NvmeSelfTestSupportedFromIdentity).Then(std::move(cb1)),
      base::BindOnce([](brillo::Error* error) {
      }).Then(base::BindOnce(std::move(cb2), false)));
}

PathLiteral GetSensorPropertyPath(SensorType sensor) {
  switch (sensor) {
    case SensorType::kBaseAccelerometer:
      return paths::cros_config::kHasBaseAccelerometer;
    case SensorType::kBaseGyroscope:
      return paths::cros_config::kHasBaseGyroscope;
    case SensorType::kBaseMagnetometer:
      return paths::cros_config::kHasBaseMagnetometer;
    case SensorType::kLidAccelerometer:
      return paths::cros_config::kHasLidAccelerometer;
    case SensorType::kLidGyroscope:
      return paths::cros_config::kHasLidGyroscope;
    case SensorType::kLidMagnetometer:
      return paths::cros_config::kHasLidMagnetometer;
    case SensorType::kBaseGravitySensor:
    case SensorType::kLidGravitySensor:
      // There are no |has-base-gravity-sensor| and |has-lid-gravity-sensor|
      // configurations.
      NOTREACHED_NORETURN();
  }
}

std::optional<bool> HasGravitySensor(std::optional<bool> has_accel,
                                     std::optional<bool> has_gyro) {
  if (!has_accel.has_value() || !has_gyro.has_value())
    return std::nullopt;
  return has_accel.value() && has_gyro.value();
}

}  // namespace

SystemConfig::SystemConfig(CrosConfig* cros_config,
                           org::chromium::debugdProxyInterface* debugd_proxy)
    : cros_config_(cros_config), debugd_proxy_(debugd_proxy) {
  CHECK(cros_config_);
  CHECK(debugd_proxy_);
}

SystemConfig::~SystemConfig() = default;

bool SystemConfig::HasBacklight() {
  auto has_backlight = cros_config_->Get(paths::cros_config::kHasBacklight);
  // Assume that device has a backlight unless otherwise configured.
  return !has_backlight || has_backlight != "false";
}

bool SystemConfig::HasBattery() {
  auto psu_type = cros_config_->Get(paths::cros_config::kPsuType);
  // Assume that device has a battery unless otherwise configured.
  return !psu_type || psu_type != "AC_only";
}

bool SystemConfig::HasSkuNumber() {
  auto has_sku_number = cros_config_->Get(paths::cros_config::kHasSkuNumber);
  // Assume that device have does NOT have a SKU number unless otherwise
  // configured.
  return has_sku_number && has_sku_number == "true";
}

bool SystemConfig::HasSmartBattery() {
  auto has_smart_battery_info =
      cros_config_->Get(paths::cros_config::kHasSmartBatteryInfo);
  // Assume that device does NOT have a smart battery unless otherwise
  // configured.
  return has_smart_battery_info && has_smart_battery_info == "true";
}

bool SystemConfig::HasPrivacyScreen() {
  auto has_privacy_screen =
      cros_config_->Get(paths::cros_config::kHasPrivacyScreen);
  return has_privacy_screen && has_privacy_screen == "true";
}

bool SystemConfig::HasChromiumEC() {
  return base::PathExists(GetRootedPath(kChromiumECPath));
}

bool SystemConfig::NvmeSupported() {
  return base::PathExists(GetRootedPath(kNvmeToolPath)) &&
         !base::FileEnumerator(GetRootedPath(kDevicePath), false,
                               base::FileEnumerator::FILES, "nvme*")
              .Next()
              .empty();
}

void SystemConfig::NvmeSelfTestSupported(
    NvmeSelfTestSupportedCallback callback) {
  auto [cb1, cb2] = base::SplitOnceCallback(std::move(callback));
  auto available_cb = base::BindOnce(&NvmeSelfTestSupportedByDebugd,
                                     debugd_proxy_, std::move(cb1));
  auto unavailable_cb = base::BindOnce(std::move(cb2), false);

  auto wait_service_cb = base::BindOnce(
      [](base::OnceClosure available_cb, base::OnceClosure unavailable_cb,
         bool service_is_available) {
        if (service_is_available) {
          std::move(available_cb).Run();
        } else {
          std::move(unavailable_cb).Run();
        }
      },
      std::move(available_cb), std::move(unavailable_cb));
  debugd_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      std::move(wait_service_cb));
}

bool SystemConfig::SmartCtlSupported() {
  return base::PathExists(GetRootedPath(kSmartctlToolPath));
}

bool SystemConfig::MmcSupported() {
  return base::PathExists(GetRootedPath(kMmcToolPath));
}

bool SystemConfig::FingerprintDiagnosticSupported() {
  auto enable =
      cros_config_->Get(paths::cros_config::fingerprint::kRoutineEnable);
  return enable && enable == "true";
}

bool SystemConfig::IsWilcoDevice() {
  const auto wilco_devices = GetWilcoBoardNames();
  return std::any_of(wilco_devices.begin(), wilco_devices.end(),
                     [](const std::string& s) -> bool {
                       // Check if the given wilco device name is a
                       // prefix for the actual board name.
                       return base::SysInfo::GetLsbReleaseBoard().rfind(s, 0) ==
                              0;
                     });
}

std::optional<std::string> SystemConfig::GetMarketingName() {
  return cros_config_->Get(paths::cros_config::kMarketingName);
}

std::optional<std::string> SystemConfig::GetOemName() {
  return cros_config_->Get(paths::cros_config::kOemName);
}

std::string SystemConfig::GetCodeName() {
  auto code_name = cros_config_->Get(paths::cros_config::kCodeName);
  // "/name" is a required field in cros config. This should not be reached in
  // normal situation. However, if in a device which is in the early development
  // stage or in a vm environment, this could still happen.
  return code_name ? code_name.value() : "";
}

std::optional<bool> SystemConfig::HasSensor(SensorType sensor) {
  // Gravity sensor is a virtual fusion sensor of accelerometer and gyroscope
  // instead of a hardware sensor. There is no static config for the gravity
  // sensor, but we can refer to the config of accelerometer and gyroscope.
  if (sensor == SensorType::kBaseGravitySensor) {
    return HasGravitySensor(HasSensor(SensorType::kBaseAccelerometer),
                            HasSensor(SensorType::kBaseGyroscope));
  } else if (sensor == SensorType::kLidGravitySensor) {
    return HasGravitySensor(HasSensor(SensorType::kLidAccelerometer),
                            HasSensor(SensorType::kLidGyroscope));
  }
  auto has_sensor = cros_config_->Get(GetSensorPropertyPath(sensor));
  if (!has_sensor) {
    return std::nullopt;
  }
  return has_sensor == "true";
}

}  // namespace diagnostics
