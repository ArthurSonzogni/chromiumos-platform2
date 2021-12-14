// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_FLASHROM_UTILS_H_
#define RMAD_UTILS_FAKE_FLASHROM_UTILS_H_

#include "rmad/utils/flashrom_utils.h"

namespace rmad {
namespace fake {

class FakeFlashromUtils : public FlashromUtils {
 public:
  FakeFlashromUtils() = default;
  ~FakeFlashromUtils() override = default;

  bool GetSoftwareWriteProtectionStatus(bool* enabled) override {
    *enabled = true;
    return true;
  }
  bool EnableSoftwareWriteProtection() override { return true; }
  bool DisableSoftwareWriteProtection() override { return true; }
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_FLASHROM_UTILS_H_
