// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_HARDWARE_VERIFIER_CLIENT_H_
#define RMAD_SYSTEM_HARDWARE_VERIFIER_CLIENT_H_

#include <rmad/proto_bindings/rmad.pb.h>

namespace rmad {

class HardwareVerifierClient {
 public:
  HardwareVerifierClient() = default;
  virtual ~HardwareVerifierClient() = default;

  virtual bool GetHardwareVerificationResult(
      HardwareVerificationResult* result) const = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_HARDWARE_VERIFIER_CLIENT_H_
