// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_hardware_verifier_utils.h"

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

bool FakeHardwareVerifierUtils::GetHardwareVerificationResult(
    HardwareVerificationResult* result) const {
  result->set_is_compliant(false);
  result->set_error_str("fake_error_string");
  return true;
}

}  // namespace rmad
