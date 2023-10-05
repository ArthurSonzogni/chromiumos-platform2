// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROS_CONFIG_CONSTANTS_H_
#define RMAD_UTILS_CROS_CONFIG_CONSTANTS_H_

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

#endif  // RMAD_UTILS_CROS_CONFIG_CONSTANTS_H_
