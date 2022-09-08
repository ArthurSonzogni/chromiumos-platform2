// Copyright 2022 The ChromiumOS Authors
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

bool FakeLibdrmUtil::FillEdidInfo(const uint32_t connector_id, EdidInfo* info) {
  if (connector_id == 0) {
    info->manufacturer = "AUO";
    info->model_id = 0x323D;
    info->manufacture_week = 20;
    info->manufacture_year = 2018;
    info->edid_version = "1.4";
    info->is_degital_input = true;
  } else {
    info->manufacturer = "DEL";
    info->model_id = 0x4231;
    info->serial_number = 1162368076;
    info->manufacture_week = 3;
    info->manufacture_year = 2022;
    info->edid_version = "1.3";
    info->is_degital_input = false;
    info->display_name = "DELL U2722DE";
  }

  return true;
}

}  // namespace diagnostics
