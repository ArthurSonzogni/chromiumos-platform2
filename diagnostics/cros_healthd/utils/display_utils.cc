// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/display_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

void FillDisplaySize(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                     const uint32_t connector_id,
                     mojom::NullableUint32Ptr* out_width,
                     mojom::NullableUint32Ptr* out_height) {
  uint32_t width;
  uint32_t height;
  if (!libdrm_util->FillDisplaySize(connector_id, &width, &height))
    return;

  *out_width = mojom::NullableUint32::New(width);
  *out_height = mojom::NullableUint32::New(height);
}

void FillDisplayResolution(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                           const uint32_t connector_id,
                           mojom::NullableUint32Ptr* out_horizontal,
                           mojom::NullableUint32Ptr* out_vertical) {
  uint32_t horizontal;
  uint32_t vertical;
  if (!libdrm_util->FillDisplayResolution(connector_id, &horizontal, &vertical))
    return;

  *out_horizontal = mojom::NullableUint32::New(horizontal);
  *out_vertical = mojom::NullableUint32::New(vertical);
}

void FillDisplayRefreshRate(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                            const uint32_t connector_id,
                            mojom::NullableDoublePtr* out_refresh_rate) {
  double refresh_rate;
  if (!libdrm_util->FillDisplayRefreshRate(connector_id, &refresh_rate))
    return;

  *out_refresh_rate = mojom::NullableDouble::New(refresh_rate);
}

mojom::ExternalDisplayInfoPtr GetExternalDisplayInfo(
    const std::unique_ptr<LibdrmUtil>& libdrm_util,
    const uint32_t connector_id) {
  auto info = mojom::ExternalDisplayInfo::New();

  FillDisplaySize(libdrm_util, connector_id, &info->display_width,
                  &info->display_height);
  FillDisplayResolution(libdrm_util, connector_id, &info->resolution_horizontal,
                        &info->resolution_vertical);
  FillDisplayRefreshRate(libdrm_util, connector_id, &info->refresh_rate);

  deprecated::EdidInfo edid_info;
  if (libdrm_util->FillEdidInfo(connector_id, &edid_info)) {
    info->manufacturer = edid_info.manufacturer;
    info->model_id = mojom::NullableUint16::New(edid_info.model_id);
    if (edid_info.serial_number.has_value())
      info->serial_number =
          mojom::NullableUint32::New(edid_info.serial_number.value());
    if (edid_info.manufacture_week.has_value())
      info->manufacture_week =
          mojom::NullableUint8::New(edid_info.manufacture_week.value());
    if (edid_info.manufacture_year.has_value())
      info->manufacture_year =
          mojom::NullableUint16::New(edid_info.manufacture_year.value());
    info->edid_version = edid_info.edid_version;
    if (edid_info.is_degital_input)
      info->input_type = mojom::DisplayInputType::kDigital;
    else
      info->input_type = mojom::DisplayInputType::kAnalog;
    info->display_name = edid_info.display_name;
  }

  return info;
}

}  // namespace diagnostics
