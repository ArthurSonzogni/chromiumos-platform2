// Copyright 2022 The ChromiumOS Authors
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
  bool FillDisplaySize(const uint32_t connector_id,
                       uint32_t* width,
                       uint32_t* height) override;
  bool FillDisplayResolution(const uint32_t connector_id,
                             uint32_t* horizontal,
                             uint32_t* vertical) override;
  bool FillDisplayRefreshRate(const uint32_t connector_id,
                              double* refresh_rate) override;
  bool FillEdidInfo(const uint32_t connector_id, EdidInfo* info) override;

  bool& initialization_success() { return initialization_success_; }
  bool& privacy_screen_supported() { return privacy_screen_supported_; }
  bool& privacy_screen_enabled() { return privacy_screen_enabled_; }

 private:
  bool initialization_success_ = true;
  bool privacy_screen_supported_ = true;
  bool privacy_screen_enabled_ = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_
