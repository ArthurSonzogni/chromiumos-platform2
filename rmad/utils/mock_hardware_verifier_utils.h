// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_HARDWARE_VERIFIER_UTILS_H_
#define RMAD_UTILS_MOCK_HARDWARE_VERIFIER_UTILS_H_

#include "rmad/utils/hardware_verifier_utils.h"

#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockHardwareVerifierUtils : public HardwareVerifierUtils {
 public:
  MockHardwareVerifierUtils() = default;
  ~MockHardwareVerifierUtils() override = default;

  MOCK_METHOD(bool,
              GetHardwareVerificationResult,
              (HardwareVerificationResult*),
              (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_HARDWARE_VERIFIER_UTILS_H_
