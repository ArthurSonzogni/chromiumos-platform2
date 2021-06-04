// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_udev_hwdb.h"

namespace diagnostics {

FakeUdevHwdb::PropertieType FakeUdevHwdb::GetProperties(
    const std::string& modalias) {
  PropertieType res;
  if (!return_empty_properties_) {
    res["ID_VENDOR_FROM_DATABASE"] = modalias;
    res["ID_MODEL_FROM_DATABASE"] = modalias;
  }
  return res;
}

}  // namespace diagnostics
