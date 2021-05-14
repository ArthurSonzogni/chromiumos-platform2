// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h"

#include "libhwsec-foundation/tpm_error/tpm_error_constants.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"
#include "libhwsec-foundation/tpm_error/tpm_error_metrics_constants.h"

namespace hwsec_foundation {

TpmErrorUmaReporter::TpmErrorUmaReporter(MetricsLibraryInterface* metrics)
    : metrics_(metrics) {}

void TpmErrorUmaReporter::Report(const TpmErrorData& data) {
  switch (data.response) {
    case kTpm1AuthFailResponse:
      metrics_->SendSparseToUMA(kTpm1AuthFailName, data.command);
      break;
    case kTpm1Auth2FailResponse:
      metrics_->SendSparseToUMA(kTpm1Auth2FailName, data.command);
      break;
    default:
      break;
  }
}

}  // namespace hwsec_foundation
