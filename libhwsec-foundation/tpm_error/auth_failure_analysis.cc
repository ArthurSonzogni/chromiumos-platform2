// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/auth_failure_analysis.h"

#include "libhwsec-foundation/tpm_error/tpm_error_constants.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"

namespace libhwsec_foundation {

bool DoesCauseDAIncrease(const TpmErrorData& data) {
  // For TPM2.0 case, the reactive trigger model of DA reset is not implemented;
  // thus, always returns `false`.
  if (USE_TPM2) {
    return false;
  }
  return data.response == kTpm1Auth2FailResponse ||
         data.response == kTpm1AuthFailResponse;
}

}  // namespace libhwsec_foundation
