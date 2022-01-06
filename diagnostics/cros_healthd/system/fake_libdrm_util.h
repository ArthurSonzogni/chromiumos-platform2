// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_

#include <vector>

#include "diagnostics/cros_healthd/system/libdrm_util.h"

namespace diagnostics {

class FakeLibdrmUtil : public LibdrmUtil {
 public:
  FakeLibdrmUtil() = default;
  FakeLibdrmUtil(const FakeLibdrmUtil& oth) = default;
  FakeLibdrmUtil(FakeLibdrmUtil&& oth) = default;
  ~FakeLibdrmUtil() override = default;

  bool Initialize() override;
  uint32_t GetEmbeddedDisplayConnectorID() override;
  std::vector<uint32_t> GetExternalDisplayConnectorID() override;
  void FillPrivacyScreenInfo(const uint32_t connector_id,
                             bool* privacy_screen_supported,
                             bool* privacy_screen_enabled) override;
  void FillDisplaySize(const uint32_t connector_id,
                       uint32_t* width,
                       uint32_t* height) override;
  void FillDisplayResolution(const uint32_t connector_id,
                             uint32_t* horizontal,
                             uint32_t* vertical) override;
  void FillDisplayRefreshRate(const uint32_t connector_id,
                              double* refresh_rate) override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_
