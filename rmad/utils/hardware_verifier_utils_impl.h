// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_HARDWARE_VERIFIER_UTILS_IMPL_H_
#define RMAD_UTILS_HARDWARE_VERIFIER_UTILS_IMPL_H_

#include "rmad/utils/hardware_verifier_utils.h"

#include <hardware_verifier/hardware_verifier.pb.h>

namespace rmad {

// Calls `hardware_verifier` command to get verification results.
class HardwareVerifierUtilsImpl : public HardwareVerifierUtils {
 public:
  HardwareVerifierUtilsImpl() = default;
  ~HardwareVerifierUtilsImpl() = default;

  bool GetHardwareVerificationResult(
      HardwareVerificationResult* result) const override;

 private:
  bool RunHardwareVerifier(
      hardware_verifier::HwVerificationReport* report) const;
};

}  // namespace rmad

#endif  // RMAD_UTILS_HARDWARE_VERIFIER_UTILS_IMPL_H_
