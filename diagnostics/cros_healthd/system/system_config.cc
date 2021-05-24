// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/system_config.h"

#include <algorithm>
#include <string>

#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>

#include "diagnostics/cros_healthd/system/system_config_constants.h"

namespace diagnostics {

namespace {

// The field that contains the bitvalue if the NVMe self test is supported by
// the device.
constexpr char kNvmeSelfTestCtrlField[] = "oacs";

// Bitmask for the bit that shows if the device supports the self test feature.
// 4th bit zero index.
constexpr uint8_t kNvmeSelfTestBitmask = 16;

}  // namespace

SystemConfig::SystemConfig(brillo::CrosConfigInterface* cros_config,
                           DebugdAdapter* debugd_adapter)
    : SystemConfig(cros_config, debugd_adapter, base::FilePath("/")) {}

SystemConfig::SystemConfig(brillo::CrosConfigInterface* cros_config,
                           DebugdAdapter* debugd_adapter,
                           const base::FilePath& root_dir)
    : cros_config_(cros_config),
      debugd_adapter_(debugd_adapter),
      root_dir_(root_dir) {
  DCHECK(cros_config_);
  DCHECK(debugd_adapter_);
}

SystemConfig::~SystemConfig() = default;

bool SystemConfig::FioSupported() {
  return base::PathExists(root_dir_.AppendASCII(kFioToolPath));
}

bool SystemConfig::HasBacklight() {
  std::string has_backlight;
  // Assume that device has a backlight unless otherwise configured.
  if (!cros_config_->GetString(kHardwarePropertiesPath, kHasBacklightProperty,
                               &has_backlight)) {
    return true;
  }
  return has_backlight != "false";
}

bool SystemConfig::HasBattery() {
  std::string psu_type;
  // Assume that device has a battery unless otherwise configured.
  if (!cros_config_->GetString(kHardwarePropertiesPath, kPsuTypeProperty,
                               &psu_type)) {
    return true;
  }
  return psu_type != "AC_only";
}

bool SystemConfig::HasSkuNumber() {
  std::string has_sku_number;
  // Assume that device have does NOT have a SKU number unless otherwise
  // configured.
  if (!cros_config_->GetString(kCachedVpdPropertiesPath, kHasSkuNumberProperty,
                               &has_sku_number)) {
    return false;
  }
  return has_sku_number == "true";
}

bool SystemConfig::HasSmartBattery() {
  std::string has_smart_battery_info;
  // Assume that device does NOT have a smart battery unless otherwise
  // configured.
  if (!cros_config_->GetString(kBatteryPropertiesPath,
                               kHasSmartBatteryInfoProperty,
                               &has_smart_battery_info)) {
    return false;
  }
  return has_smart_battery_info == "true";
}

bool SystemConfig::NvmeSupported() {
  return base::PathExists(root_dir_.AppendASCII(kNvmeToolPath)) &&
         !base::FileEnumerator(root_dir_.AppendASCII(kDevicePath), false,
                               base::FileEnumerator::FILES, "nvme*")
              .Next()
              .empty();
}

bool SystemConfig::NvmeSelfTestSupported() {
  auto result = debugd_adapter_->GetNvmeIdentitySync();
  if (result.error.get())
    return false;

  // Example output:
  // oacs      : 0x17
  // acl       : 3
  // aerl      : 7
  // frmw      : 0x16
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(result.value, ':', '\n', &pairs);
  for (auto& p : pairs) {
    if (base::TrimWhitespaceASCII(p.first,
                                  base::TrimPositions::TRIM_TRAILING) !=
        kNvmeSelfTestCtrlField) {
      continue;
    }

    u_int32_t value;
    if (!base::HexStringToUInt(
            base::TrimWhitespaceASCII(p.second, base::TrimPositions::TRIM_ALL),
            &value)) {
      return false;
    }

    // Check to see if the device-self-test support bit is set
    return ((value & kNvmeSelfTestBitmask) == kNvmeSelfTestBitmask);
  }

  return false;
}

bool SystemConfig::SmartCtlSupported() {
  return base::PathExists(root_dir_.AppendASCII(kSmartctlToolPath));
}

bool SystemConfig::IsWilcoDevice() {
  const auto wilco_devices = GetWilcoBoardNames();
  return std::count(wilco_devices.begin(), wilco_devices.end(),
                    base::SysInfo::GetLsbReleaseBoard());
}

base::Optional<std::string> SystemConfig::GetMarketingName() {
  std::string marketing_name;
  if (!cros_config_->GetString(kArcBuildPropertiesPath, kMarketingNameProperty,
                               &marketing_name)) {
    return base::nullopt;
  }
  return marketing_name;
}

std::string SystemConfig::GetCodeName() {
  std::string code_name;
  if (!cros_config_->GetString(kRootPath, kCodeNameProperty, &code_name)) {
    // "/name" is a required field in cros config. This should not be reached in
    // normal situation. However, if in a device which is in the early
    // development stage or in a vm environment, this could still happen.
    return "";
  }
  return code_name;
}

}  // namespace diagnostics
