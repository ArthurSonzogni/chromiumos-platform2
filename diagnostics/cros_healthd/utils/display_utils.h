// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_DISPLAY_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_DISPLAY_UTILS_H_

#include <memory>

#include "diagnostics/cros_healthd/system/libdrm_util.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

void FillDisplaySize(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                     const uint32_t connector_id,
                     ash::cros_healthd::mojom::NullableUint32Ptr* out_width,
                     ash::cros_healthd::mojom::NullableUint32Ptr* out_height);

void FillDisplayResolution(
    const std::unique_ptr<LibdrmUtil>& libdrm_util,
    const uint32_t connector_id,
    ash::cros_healthd::mojom::NullableUint32Ptr* out_horizontal,
    ash::cros_healthd::mojom::NullableUint32Ptr* out_vertical);

void FillDisplayRefreshRate(
    const std::unique_ptr<LibdrmUtil>& libdrm_util,
    const uint32_t connector_id,
    ash::cros_healthd::mojom::NullableDoublePtr* out_refresh_rate);

ash::cros_healthd::mojom::ExternalDisplayInfoPtr GetExternalDisplayInfo(
    const std::unique_ptr<LibdrmUtil>& libdrm_util,
    const uint32_t connector_id);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_DISPLAY_UTILS_H_
