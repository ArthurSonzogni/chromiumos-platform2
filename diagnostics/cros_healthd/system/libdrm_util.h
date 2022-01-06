// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_H_

#include <cstdint>

namespace diagnostics {

// Interface for accessing the libdrm library.
class LibdrmUtil {
 public:
  virtual ~LibdrmUtil() = default;

  virtual bool Initialize() = 0;
  virtual uint32_t GetEmbeddedDisplayConnectorID() = 0;
  virtual void FillPrivacyScreenInfo(const uint32_t connector_id,
                                     bool* privacy_screen_supported,
                                     bool* privacy_screen_enabled) = 0;
  virtual void FillDisplaySize(const uint32_t connector_id,
                               uint32_t* width,
                               uint32_t* height) = 0;
  virtual void FillDisplayResolution(const uint32_t connector_id,
                                     uint32_t* horizontal,
                                     uint32_t* vertical) = 0;
  virtual void FillDisplayRefreshRate(const uint32_t connector_id,
                                      double* refresh_rate) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_H_
