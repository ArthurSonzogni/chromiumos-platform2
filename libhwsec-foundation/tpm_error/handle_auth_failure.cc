// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/handle_auth_failure.h"

#include "libhwsec-foundation/da_reset/da_resetter.h"
#include "libhwsec-foundation/tpm_error/auth_failure_analysis.h"
#include "libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h"

extern "C" int HandleAuthFailure(const struct TpmErrorData* data) {
  if (!libhwsec_foundation::DoesCauseDAIncrease(*data)) {
    return true;
  }

  libhwsec_foundation::TpmErrorUmaReporter reporter;
  reporter.Report(*data);

  libhwsec_foundation::DAResetter resetter;
  return resetter.ResetDictionaryAttackLock();
}
