// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_properties.h"

#include <optional>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>

namespace rmad {

namespace {

std::optional<std::string> ReadProperty(const base::FilePath& property_path) {
  std::optional<std::string> ret = std::nullopt;
  if (std::string value; base::ReadFileToString(property_path, &value)) {
    ret = value;
  }
  return ret;
}

std::string StringDescription(const std::optional<std::string>& value) {
  return value.has_value() ? value.value() : "N/A";
}

std::string StringNotEmptyDescription(const std::optional<std::string>& value) {
  return (value.has_value() && value.value() != "") ? "Yes" : "No";
}

std::string StringIsExpectedDescription(const std::optional<std::string>& value,
                                        const std::string& expect) {
  return (value.has_value() && value.value() == expect) ? "Yes" : "No";
}

// Specifically for stylus.
std::string StylusDescription(const std::optional<std::string>& value) {
  return (value.has_value() && value.value() != "unknown") ? value.value()
                                                           : "N/A";
}

// Specifically for cellular. The Firmware variant name can be "<model>" or
// "<model>_<chip>".
std::string CellularDescription(const std::optional<std::string>& value) {
  if (!value.has_value()) {
    return "No";
  }
  // Firmware variant is <model>_<chip>. Only return the <chip> part.
  if (auto pos = value.value().find('_'); pos != std::string::npos) {
    return base::StringPrintf("Yes(%s)", value.value().substr(pos + 1).c_str());
  }
  // Firmware variant is <model>. Just return "Yes".
  return "Yes";
}

bool Has1080pSupport(const base::FilePath& camera_devices_path) {
  base::FileEnumerator e(camera_devices_path,
                         /*recursive=*/false,
                         base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath device_path = e.Next(); !device_path.empty();
       device_path = e.Next()) {
    base::FilePath path = device_path.Append(kCrosCameraFlagsPath)
                              .Append(kCrosCameraSupport1080pKey);
    std::optional<std::string> value = ReadProperty(path);
    if (value.has_value() && value.value() == "true") {
      return true;
    }
  }
  return false;
}

std::string GetWifiType(const base::FilePath& wifi_path) {
  if (base::PathExists(wifi_path.Append(kCrosWifiAth10kTabletModePath)) ||
      base::PathExists(wifi_path.Append(kCrosWifiAth10kNonTabletModePath))) {
    return "Qualcomm";
  }
  if (base::PathExists(wifi_path.Append(kCrosWifiRtwTabletModePath)) ||
      base::PathExists(wifi_path.Append(kCrosWifiRtwNonTabletModePath))) {
    return "Realtek";
  }
  if (base::PathExists(wifi_path.Append(kCrosWifiMtkTabletModePath)) ||
      base::PathExists(wifi_path.Append(kCrosWifiMtkNonTabletModePath))) {
    return "MediaTek";
  }
  if (base::PathExists(wifi_path.Append(kCrosWifiSarFilePath))) {
    return "Intel";
  }
  return "N/A";
}

}  // namespace

std::string GetHasTouchscreenDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesHasTouchscreenKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Touchscreen:") +
         StringIsExpectedDescription(value, "true");
}

std::string GetHasPrivacyScreenDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesHasPrivacyScreenKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("PrivacyScreen:") +
         StringIsExpectedDescription(value, "true");
}

std::string GetHasHdmiDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesHasHdmiKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("HDMI:") + StringIsExpectedDescription(value, "true");
}

std::string GetHasSdReaderDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesHasSdReaderKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("SDReader:") + StringIsExpectedDescription(value, "true");
}

std::string GetStylusCategoryDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesStylusCategoryKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Stylus:") + StylusDescription(value);
}

std::string GetFormFactorDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesFormFactorKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("FormFactor:") + StringDescription(value);
}

std::string GetStorageTypeDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesStorageTypeKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Storage:") + StringDescription(value);
}

std::string GetCellularDescription(const base::FilePath& root_path) {
  base::FilePath path =
      root_path.Append(kCrosModemPath).Append(kCrosModemFirmwareVariantKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Cellular:") + CellularDescription(value);
}

std::string GetHasFingerprintDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosFingerprintPath)
                            .Append(kCrosFingerprintSensorLocationKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Fingerprint:") + StringNotEmptyDescription(value);
}

std::string GetAudioUcmSuffixDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosAudioPath)
                            .Append(kCrosAudioMainPath)
                            .Append(kCrosAudioUcmSuffixKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Audio:") + StringDescription(value);
}

std::string GetAudioCrasConfigDirDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosAudioPath)
                            .Append(kCrosAudioMainPath)
                            .Append(kCrosAudioCrasConfigDirKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("CrasConfig:") + StringDescription(value);
}

std::string GetHasKeyboardBacklightDescription(
    const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosPowerPath)
                            .Append(kCrosPowerHasKeyboardBacklightKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("KeyboardBacklight:") +
         StringIsExpectedDescription(value, "1");
}

std::string GetCameraCountDescription(const base::FilePath& root_path) {
  base::FilePath path =
      root_path.Append(kCrosCameraPath).Append(kCrosCameraCountKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("CameraCount:") + StringDescription(value);
}

std::string GetHas1080pCameraDescription(const base::FilePath& root_path) {
  base::FilePath camera_devices_path =
      root_path.Append(kCrosCameraPath).Append(kCrosCameraDevicesPath);
  return std::string("Camera1080p:") +
         (Has1080pSupport(camera_devices_path) ? "Yes" : "No");
}

std::string GetHasProximitySensorDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosProximitySensorPath);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("ProximitySensor:") +
         (base::PathExists(path) ? "Yes" : "No");
}

std::string GetWifiDescription(const base::FilePath& root_path) {
  base::FilePath wifi_path = root_path.Append(kCrosWifiPath);
  return std::string("Wifi:") + GetWifiType(wifi_path);
}

}  // namespace rmad
