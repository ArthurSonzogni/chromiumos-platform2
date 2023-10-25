// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROS_CONFIG_PROPERTIES_H_
#define RMAD_UTILS_CROS_CONFIG_PROPERTIES_H_

#include <string>

#include <base/files/file_path.h>

namespace rmad {

// cros_config root path.
constexpr char kCrosRootPath[] = "/";
// cros_config property /name.
constexpr char kCrosModelNameKey[] = "name";
// cros_config property /brand-code.
constexpr char kCrosBrandCodeKey[] = "brand-code";

// cros_config path /identity.
constexpr char kCrosIdentityPath[] = "identity";
// cros_config property /identity/sku-id.
constexpr char kCrosIdentitySkuKey[] = "sku-id";
// cros_config property /identity/custom-label-tag.
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";

// cros_config path /firmware.
constexpr char kCrosFirmwarePath[] = "firmware";
// cros_config property /firmware/firmware-config.
constexpr char kCrosFirmwareFirmwareConfigKey[] = "firmware-config";

// cros_config path /hardware-properties.
constexpr char kCrosHardwarePropertiesPath[] = "hardware-properties";
// cros_config property /hardware-properties/has-touchscreen.
constexpr char kCrosHardwarePropertiesHasTouchscreenKey[] = "has-touchscreen";
std::string GetHasTouchscreenDescription(const base::FilePath& root_path);
// cros_config property /hardware-properties/has-privacy-screen.
constexpr char kCrosHardwarePropertiesHasPrivacyScreenKey[] =
    "has-privacy-screen";
std::string GetHasPrivacyScreenDescription(const base::FilePath& root_path);
// cros_config property /hardware-properties/has-hdmi.
constexpr char kCrosHardwarePropertiesHasHdmiKey[] = "has-hdmi";
std::string GetHasHdmiDescription(const base::FilePath& root_path);
// cros_config property /hardware-properties/has-sd-reader.
constexpr char kCrosHardwarePropertiesHasSdReaderKey[] = "has-sd-reader";
std::string GetHasSdReaderDescription(const base::FilePath& root_path);
// cros_config property /hardware-properties/stylus-category.
constexpr char kCrosHardwarePropertiesStylusCategoryKey[] = "stylus-category";
std::string GetStylusCategoryDescription(const base::FilePath& root_path);
// cros_config property /hardware-properties/form-factor.
constexpr char kCrosHardwarePropertiesFormFactorKey[] = "form-factor";
std::string GetFormFactorDescription(const base::FilePath& root_path);
// cros_config property /hardware-properties/storage-type.
constexpr char kCrosHardwarePropertiesStorageTypeKey[] = "storage-type";
std::string GetStorageTypeDescription(const base::FilePath& root_path);

// cros_config path /modem.
constexpr char kCrosModemPath[] = "modem";
// cros_config property /modem/firmware-variant.
constexpr char kCrosModemFirmwareVariantKey[] = "firmware-variant";
std::string GetCellularDescription(const base::FilePath& root_path);

// cros_config path /fingerprint.
constexpr char kCrosFingerprintPath[] = "fingerprint";
// cros_config property /fingerprint/sensor-location.
constexpr char kCrosFingerprintSensorLocationKey[] = "sensor-location";
std::string GetHasFingerprintDescription(const base::FilePath& root_path);

// cros_config path /audio.
constexpr char kCrosAudioPath[] = "audio";
// cros_config path /audio/main.
constexpr char kCrosAudioMainPath[] = "main";
// cros_config property /audio/main/ucm-suffix.
constexpr char kCrosAudioUcmSuffixKey[] = "ucm-suffix";
std::string GetAudioDescription(const base::FilePath& root_path);

// cros_config path /power.
constexpr char kCrosPowerPath[] = "power";
// cros_config property /power/has-keyboard-backlight.
constexpr char kCrosPowerHasKeyboardBacklightKey[] = "has-keyboard-backlight";
std::string GetHasKeyboardBacklightDescription(const base::FilePath& root_path);

// cros_config path /camera.
constexpr char kCrosCameraPath[] = "camera";
// cros_config property /camera/count.
constexpr char kCrosCameraCountKey[] = "count";
std::string GetCameraCountDescription(const base::FilePath& root_path);

// cros_config path /proximity-sensor.
constexpr char kCrosProximitySensor[] = "proximity-sensor";
std::string GetHasProximitySensorDescription(const base::FilePath& root_path);

// cros_config path /rmad.
constexpr char kCrosRmadPath[] = "rmad";
// cros_config property /rmad/enabled.
constexpr char kCrosRmadEnabledKey[] = "enabled";
// cros_config property /rmad/has-cbi.
constexpr char kCrosRmadHasCbiKey[] = "has-cbi";
// cros_config property /rmad/use-legacy-custom-label.
constexpr char kCrosRmadUseLegacyCustomLabelKey[] = "use-legacy-custom-label";
// cros_config path /rmad/ssfc.
constexpr char kCrosSsfcPath[] = "ssfc";
// cros_config property /ssfc/mask.
constexpr char kCrosSsfcMaskKey[] = "mask";
// cros_config path /rmad/ssfc/component-type-configs.
constexpr char kCrosComponentTypeConfigsPath[] = "component-type-configs";
// cros_config property /rmad/ssfc/component-type-configs/*/component-type.
constexpr char kCrosComponentTypeConfigsComponentTypeKey[] = "component-type";
// cros_config property /rmad/ssfc/component-type-configs/*/default-value.
constexpr char kCrosComponentTypeConfigsDefaultValueKey[] = "default-value";
// cros_config path /rmad/ssfc/component-type-configs/*/probeable-components.
constexpr char kCrosProbeableComponentsPath[] = "probeable-components";
// cros_config property
// /rmad/ssfc/component-type-configs/*/probeable-components/*/identifier.
constexpr char kCrosProbeableComponentsIdentifierKey[] = "identifier";
// cros_config property
// /rmad/ssfc/component-type-configs/*/probeable-components/*/value.
constexpr char kCrosProbeableComponentsValueKey[] = "value";

}  // namespace rmad

#endif  // RMAD_UTILS_CROS_CONFIG_PROPERTIES_H_
