// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_HARDWARE_VERIFIER_UTILS_H_
#define RMAD_UTILS_HARDWARE_VERIFIER_UTILS_H_

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

class HardwareVerifierUtils {
 public:
  HardwareVerifierUtils() = default;
  virtual ~HardwareVerifierUtils() = default;

  virtual bool GetHardwareVerificationResult(
      HardwareVerificationResult* result) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_HARDWARE_VERIFIER_UTILS_H_
