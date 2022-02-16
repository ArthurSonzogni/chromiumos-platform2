// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "diagnostics/cros_healthd/system/fake_libdrm_util.h"

namespace diagnostics {

bool FakeLibdrmUtil::Initialize() {
  return true;
}

uint32_t FakeLibdrmUtil::GetEmbeddedDisplayConnectorID() {
  return 0;
}

std::vector<uint32_t> FakeLibdrmUtil::GetExternalDisplayConnectorID() {
  return {1, 2};
}

void FakeLibdrmUtil::FillPrivacyScreenInfo(const uint32_t connector_id,
                                           bool* privacy_screen_supported,
                                           bool* privacy_screen_enabled) {
  *privacy_screen_supported = true;
  *privacy_screen_enabled = false;
}

bool FakeLibdrmUtil::FillDisplaySize(const uint32_t connector_id,
                                     uint32_t* width,
                                     uint32_t* height) {
  if (connector_id == 0) {
    *width = 290;
    *height = 190;
  } else {
    *width = 600;
    *height = 340;
  }

  return true;
}

bool FakeLibdrmUtil::FillDisplayResolution(const uint32_t connector_id,
                                           uint32_t* horizontal,
                                           uint32_t* vertical) {
  if (connector_id == 0) {
    *horizontal = 1920;
    *vertical = 1080;
  } else {
    *horizontal = 2560;
    *vertical = 1440;
  }

  return true;
}

bool FakeLibdrmUtil::FillDisplayRefreshRate(const uint32_t connector_id,
                                            double* refresh_rate) {
  if (connector_id == 0) {
    *refresh_rate = 60.0;
  } else {
    *refresh_rate = 120.0;
  }

  return true;
}

}  // namespace diagnostics
