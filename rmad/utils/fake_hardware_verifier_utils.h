// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_HARDWARE_VERIFIER_UTILS_H_
#define RMAD_UTILS_FAKE_HARDWARE_VERIFIER_UTILS_H_

#include "rmad/utils/hardware_verifier_utils.h"

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

// A fake |HardwareVerifierUtils| that always returns the same verification
// result.
class FakeHardwareVerifierUtils : public HardwareVerifierUtils {
 public:
  FakeHardwareVerifierUtils() = default;
  ~FakeHardwareVerifierUtils() override = default;

  bool GetHardwareVerificationResult(
      HardwareVerificationResult* result) const override;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_HARDWARE_VERIFIER_UTILS_H_
