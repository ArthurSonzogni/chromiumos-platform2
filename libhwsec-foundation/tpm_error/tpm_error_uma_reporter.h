// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TPM_ERROR_TPM_ERROR_UMA_REPORTER_H_
#define LIBHWSEC_FOUNDATION_TPM_ERROR_TPM_ERROR_UMA_REPORTER_H_

#include <metrics/metrics_library.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"

namespace hwsec_foundation {

// Reports various types of UMA regarding to TPM errors.
class HWSEC_FOUNDATION_EXPORT TpmErrorUmaReporter {
 public:
  TpmErrorUmaReporter() = default;
  // Constructs the object with injected `metrics`; used for testing.
  explicit TpmErrorUmaReporter(MetricsLibraryInterface* metrics);
  ~TpmErrorUmaReporter() = default;

  // Not copyable or movable.
  TpmErrorUmaReporter(const TpmErrorUmaReporter&) = delete;
  TpmErrorUmaReporter& operator=(const TpmErrorUmaReporter&) = delete;
  TpmErrorUmaReporter(TpmErrorUmaReporter&&) = delete;
  TpmErrorUmaReporter& operator=(TpmErrorUmaReporter&&) = delete;

  // Reports the UMAs according to the error indicated in `data`, if necessary.
  void Report(const TpmErrorData& data);

 private:
  MetricsLibrary default_metrics_;
  MetricsLibraryInterface* metrics_ = &default_metrics_;
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TPM_ERROR_TPM_ERROR_UMA_REPORTER_H_
