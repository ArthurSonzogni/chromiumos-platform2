// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_libdrm_util.h"

namespace diagnostics {

bool FakeLibdrmUtil::Initialize() {
  return true;
}

uint32_t FakeLibdrmUtil::GetEmbeddedDisplayConnectorID() {
  return 0;
}

void FakeLibdrmUtil::FillPrivacyScreenInfo(const uint32_t connector_id,
                                           bool* privacy_screen_supported,
                                           bool* privacy_screen_enabled) {
  *privacy_screen_supported = true;
  *privacy_screen_enabled = false;
}

}  // namespace diagnostics
