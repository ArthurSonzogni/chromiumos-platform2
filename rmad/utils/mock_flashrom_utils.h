// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_FLASHROM_UTILS_H_
#define RMAD_UTILS_MOCK_FLASHROM_UTILS_H_

#include "rmad/utils/flashrom_utils.h"

#include <gmock/gmock.h>

namespace rmad {

class MockFlashromUtils : public FlashromUtils {
 public:
  MockFlashromUtils() = default;
  ~MockFlashromUtils() override = default;

  MOCK_METHOD(bool, EnableSoftwareWriteProtection, (), (override));
  MOCK_METHOD(bool, DisableSoftwareWriteProtection, (), (override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_FLASHROM_UTILS_H_
