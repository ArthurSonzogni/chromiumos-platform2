// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

class DisplayUtil {
 public:
  virtual ~DisplayUtil() = default;

  virtual std::optional<uint32_t> GetEmbeddedDisplayConnectorID() = 0;
  virtual std::vector<uint32_t> GetExternalDisplayConnectorIDs() = 0;
  virtual void FillPrivacyScreenInfo(const uint32_t connector_id,
                                     bool* privacy_screen_supported,
                                     bool* privacy_screen_enabled) = 0;
  virtual ::ash::cros_healthd::mojom::ExternalDisplayInfoPtr
  GetExternalDisplayInfo(const uint32_t connector_id) = 0;
  virtual ::ash::cros_healthd::mojom::EmbeddedDisplayInfoPtr
  GetEmbeddedDisplayInfo() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_H_
